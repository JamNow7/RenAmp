/*
 * Renamp — DSP Chain
 * Purpose: Centralizes audio processing pipeline and exposes parameterized stages.
 * Real-time safety: No dynamic allocations, no locks, and no I/O in process(); safe for audio callback.
 * Threading model: Control thread updates parameters via component accessors; audio thread calls process();
 *                 atomic fields protect cross-thread updates.
 * Dependencies: gate.h, saturation.h, nam_processor.h, cabinet_processor.h, limiter.h, parameters/param_smoother.h
 *
 * Runtime order (2026-06-18): NAM → Cabinet → Master Gain → Limiter (optional).
 * Gate and Saturation are initialized but not yet wired in process() pending QA validation.
 *
 * Design note: All components are initialized even when disconnected.
 * Why: Keeps parameter surfaces stable and avoids reallocations/threading hazards when enabling stages later.
 */
#pragma once

#include "gate.h"
#include "saturation.h"
#include "nam_processor.h"
#include "cabinet_processor.h"
#include "limiter.h"
#include "parameters/param_smoother.h"

namespace RenaAmp {

/**
 * @brief Renamp DSP chain — audio processing pipeline
 *
 * Current runtime order (2026-06-18): NAM → Cabinet → Master Gain → Limiter.
 * Gate and Saturation stages are initialized but not yet wired in process() pending QA.
 *
 * RT-Safe: No allocations, locks, or I/O in process()
 */
class DSPChain {
public:
    DSPChain() = default;
    ~DSPChain() = default;

    /**
     * @brief Initialize all DSP components
     * @param sample_rate Sample rate in Hz
     * @pre sample_rate > 0
     * @post All stages initialized and ready for process(); components can receive parameters
     * @note Initializes disconnected stages (Gate/Saturation) to maintain stable parameter surfaces
     */
    void initialize(float sample_rate);

    /**
     * @brief Process a block of stereo samples in-place
     * @param left Left channel buffer (size: count)
     * @param right Right channel buffer (size: count)
     * @param count Number of samples per channel
     * @pre Buffers are non-null, 32-bit float
     * @post Audio processed in-place; no allocations/locks performed
     * @note RT-Safe: callable from audio callback thread
     *
     * Current processing path: NAM → Cabinet → Master Gain → Limiter (if enabled)
     */
    void process(float* left, float* right, size_t count);

    /**
     * @brief Set master gain (post NAM/IR, pre-Limiter)
     * @param gain_db Gain in dB, range: -20 to +20 (default 0 dB)
     * @param smooth_time Smoothing duration in seconds (default 0.02s)
     *
     * Position: After NAM and Cabinet to avoid re-exciting the model/IR when changing loudness.
     */
    void setMasterGain(float gain_db, float smooth_time = 0.02f);

    /**
     * @brief Enable or disable the limiter stage
     * @param enabled true to enable, false to disable (default: false)
     * @note Typical guitar workflow keeps limiter off; enable for line-level output or to prevent clipping
     */
    void setLimiterEnabled(bool enabled) { limiter_enabled_ = enabled; }

    /**
     * @brief Access individual DSP components for parameter control
     * @return Reference to component (no ownership transfer)
     * @note Control thread uses these to set parameters; audio thread reads during process()
     * @note Components are valid even when not wired in the active processing path
     */
    Gate& gate() { return gate_; }
    Saturation& saturation() { return saturation_; }
    NAMProcessor& nam() { return nam_; }
    CabinetProcessor& cabinet() { return cabinet_; }
    Limiter& limiter() { return limiter_; }

    /**
     * @brief Const accessors for monitoring (UI/metering only)
     * @note NOT RT-safe for audio processing; use from control thread only
     */
    const Gate& gate() const { return gate_; }
    const Saturation& saturation() const { return saturation_; }
    const NAMProcessor& nam() const { return nam_; }
    const CabinetProcessor& cabinet() const { return cabinet_; }
    const Limiter& limiter() const { return limiter_; }

private:
    // DSP components in declaration order (processing order documented above)
    Gate gate_;
    Saturation saturation_;
    NAMProcessor nam_;
    CabinetProcessor cabinet_;
    Limiter limiter_;

    // Master gain in dB (smoothed), applied after Cabinet, before Limiter
    ParameterSmoother master_gain_db_;

    // Limiter enable flag (default: off for natural guitar dynamics)
    bool limiter_enabled_{false};
};

} // namespace RenaAmp
