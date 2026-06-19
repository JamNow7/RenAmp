/*
 * RenAmp — Gate
 *
 * Purpose:
 * Stereo-linked noise gate with hysteresis and smoothed parameters.
 * Reduces noise and hum by attenuating signal below a dynamic threshold.
 *
 * Real-time safety:
 * process() is RT-safe:
 * - No allocations
 * - No locks or mutexes
 * - No file I/O
 * - No logging or syscalls
 * - No exception throwing
 *
 * All parameter changes are received through ParameterSmoother,
 * which performs lock-free atomic updates and smooth transitions.
 *
 * Memory ownership:
 * All audio state (envelope, gain, gate state, coefficients) is owned
 * exclusively by the audio thread. The control thread only writes
 * target values via ParameterSmoother; it never touches RT state directly.
 *
 * Threading model:
 * - Control thread: setThreshold(), setAttack(), setRelease(), setHysteresis()
 * - Audio callback thread: process()
 * - Cross-thread communication: ParameterSmoother (atomic target → smoothed ramp)
 *
 * Processing model:
 * Per-sample stereo processing with shared envelope follower.
 * Both channels use the same gain reduction to preserve stereo imaging.
 *
 * Coefficients derived from smoothed parameters are updated once per block
 * and applied per-sample inside the audio thread.
 *
 * DSP chain position:
 * Planned as a pre-NAM stage (input noise suppression before model inference).
 * Currently not connected to the active signal chain.
 *
 * Design decisions:
 * Hysteresis avoids rapid toggling around threshold by introducing
 * separate open/close thresholds.
 *
 * This is a deliberate trade-off:
 * - Prevents chattering and audible gate flicker
 * - Improves stability on noisy guitar inputs
 * - Avoids lookahead to maintain zero latency for embedded targets
 *
 * Trade-off:
 * No lookahead or soft-knee modeling → prioritizes deterministic latency
 * over advanced musical gating behavior.
 *
 * Future enhancement path:
 * - Sidechain input support
 * - Lookahead buffer (optional latency mode)
 * - Soft-knee / range-based attenuation instead of full gating
 */
#pragma once

#include <atomic>
#include <cstdint>
#include "parameters/param_smoother.h"

namespace RenaAmp {

/**
 * @brief Stereo-linked noise gate with threshold and hysteresis
 *
 * RT-Safe:
 * Must not perform allocations, locks, I/O, logging, or syscalls
 * inside process() or any audio-thread function.
 *
 * Visualization methods (isOpen, getGainReduction) are NOT RT-critical
 * and should not be used inside the audio callback.
 *
 * Features:
 * - Threshold-based gating
 * - Hysteresis to prevent rapid toggling near threshold
 * - Smooth attack/release envelope control
 * - Stereo-linked envelope follower
 * - Smooth parameter transitions via ParameterSmoother
 */
class Gate {
public:
    Gate();
    ~Gate() = default;

    /**
     * @brief Initialize gate with sample rate
     * @param sample_rate Audio sample rate in Hz
     *
     * Precondition: Must be called from control thread before audio start.
     * Postcondition: Gate is ready for real-time processing.
     */
    void initialize(float sample_rate);

    /**
     * @brief Process a block of stereo samples in-place
     * @param left Left channel buffer (size: count)
     * @param right Right channel buffer (size: count)
     * @param count Number of samples per channel
     *
     * RT-Safe:
     * - No allocations
     * - No locks
     * - No I/O
     * - No logging
     * - No exceptions
     *
     * Thread: Audio callback thread only
     */
    void process(float* left, float* right, size_t count);

    /**
     * @brief Set threshold level (in dB, negative values)
     * @param threshold_db -60 dB = fully closed, 0 dB = always open
     * @param smooth_time smoothing time in seconds (default 10ms)
     */
    void setThreshold(float threshold_db, float smooth_time = 0.01f);

    /**
     * @brief Set attack time (ms)
     * Controls how quickly the gate opens when signal exceeds threshold.
     */
    void setAttack(float attack_ms);

    /**
     * @brief Set release time (ms)
     * Controls how quickly the gate closes when signal drops below threshold.
     */
    void setRelease(float release_ms);

    /**
     * @brief Set hysteresis offset (dB)
     * Creates separate open/close thresholds to prevent chatter.
     */
    void setHysteresis(float hysteresis_db);

    /**
     * @brief Check if gate is currently open (UI only)
     *
     * NOT RT-safe for audio processing usage.
     * Intended for visualization/meters only.
     */
    bool isOpen() const;

    /**
     * @brief Get current gain reduction in dB (UI only)
     *
     * NOT RT-safe for audio processing usage.
     */
    float getGainReduction() const;

private:
    // Parameter smoothers (control thread → audio thread safe bridge)
    ParameterSmoother threshold_smoother_;
    ParameterSmoother attack_smoother_;
    ParameterSmoother release_smoother_;
    ParameterSmoother hysteresis_smoother_;

    // Audio-thread owned state (never written from control thread)
    float envelope_{0.0f};          ///< Envelope follower state
    float current_gain_{1.0f};      ///< Current applied gain
    bool gate_open_{true};          ///< Current gate state (audio thread only)
    float sample_rate_{48000.0f};   ///< Sample rate

    // Derived coefficients (updated per block, used per sample)
    float attack_coeff_{0.0f};
    float release_coeff_{0.0f};

    // Metering state (audio thread → UI thread)
    float gain_reduction_db_{0.0f};

    /**
     * @brief Convert dB to linear amplitude
     * Used for gain computation in envelope processing.
     */
    static float dbToLinear(float db);

    /**
     * @brief Convert linear amplitude to dB
     * Used for metering only (floored for stability).
     */
    static float linearToDb(float linear);

    /**
     * @brief Update derived coefficients from smoothed parameters
     *
     * Called once per process() block before sample loop.
     * Ensures stable per-sample processing with consistent coefficients.
     */
    void updateCoefficients();
};

} // namespace RenaAmp
