/*
 * RenAmp — Limiter
 *
 * Purpose:
 * Peak limiter with soft-knee curve and independent gain smoothing.
 * Prevents output clipping and inter-sample peaks in the final DSP stage.
 *
 * Real-time safety:
 * process() is RT-safe:
 * - No allocations
 * - No locks or mutexes
 * - No file I/O
 * - No logging or syscalls
 * - No exception throwing
 *
 * All parameter changes arrive via ParameterSmoother (lock-free atomics).
 * Gain smoothing uses a fixed 5ms time constant, independent of the envelope
 * detector to prevent pumping artifacts caused by rapid parameter changes.
 *
 * Memory ownership:
 * All audio state (envelope, gain reduction, smoothing state) is owned
 * exclusively by the audio thread. Control thread only writes target values
 * via ParameterSmoother and never accesses RT state.
 *
 * Threading model:
 * - Control thread: setCeiling(), setRelease(), setKnee()
 * - Audio callback thread: process()
 * - Cross-thread communication: ParameterSmoother (atomic target → smooth ramp)
 *
 * Processing model:
 * Per-sample stereo processing with linked peak detection.
 * Both channels share the same gain reduction to preserve stereo image.
 *
 * DSP chain position:
 * Final stage in the DSP chain, after MasterGain.
 * Optional stage (disabled by default) for:
 * - output safety limiting
 * - inter-sample peak prevention
 * - line-level output compliance
 *
 * Design decisions:
 * Soft-knee curve uses quadratic interpolation between knee region bounds.
 * This avoids abrupt gain transitions that cause distortion artifacts.
 *
 * This is a deliberate trade-off:
 * - Improves perceived transparency of limiting
 * - Reduces harmonic distortion compared to hard clipping
 * - Slightly increases CPU cost vs hard limiter
 *
 * Oversampling / true-peak strategy:
 * True-peak detection (2x oversampling) is implemented but not yet wired
 * into the active processing path.
 *
 * This allows future activation for:
 * - broadcast-grade output compliance
 * - mastering workflows
 * - strict inter-sample peak control
 *
 * Known implementation note:
 * Internal computeGain() uses threshold values in linear form only.
 * All dB conversions are performed at parameter entry points to avoid
 * double-conversion inside the audio loop.
 *
 * Future enhancement path:
 * - Enable true-peak detection in process()
 * - Add 4x/8x oversampling for mastering-grade limiting
 * - Add lookahead mode (optional latency path)
 * - Add external sidechain input for advanced workflows
 */

#pragma once

#include <atomic>
#include "parameters/param_smoother.h"

namespace RenaAmp {

/**
 * @brief Peak limiter with soft knee and gain smoothing
 *
 * RT-Safe:
 * Must not perform allocations, locks, I/O, logging, or syscalls
 * inside process() or any audio-thread function.
 *
 * Visualization method (getGainReduction) is NOT RT-critical
 * and must only be used from UI / control thread.
 *
 * Features:
 * - Soft-knee limiting (quadratic interpolation)
 * - Stereo-linked peak detection
 * - Independent gain smoothing (5ms fixed time constant)
 * - Ceiling, release, knee control
 * - True-peak detection (implemented but not yet active)
 */
class Limiter {
public:
    Limiter();
    ~Limiter() = default;

    /**
     * @brief Initialize limiter with sample rate
     * @param sample_rate Audio sample rate in Hz
     *
     * Precondition: Must be called before audio starts (control thread).
     * Postcondition: Limiter is ready for real-time processing.
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
     * @brief Set ceiling (max output level in dB)
     * @param ceiling_db Range: -20 dB to 0 dB (default -2.5 dB)
     * @param smooth_time smoothing time in seconds (default 20ms)
     */
    void setCeiling(float ceiling_db, float smooth_time = 0.02f);

    /**
     * @brief Set release time (ms)
     * Controls recovery speed after limiting.
     */
    void setRelease(float release_ms, float smooth_time = 0.01f);

    /**
     * @brief Set knee width (dB)
     * Controls softness of transition into limiting region.
     */
    void setKnee(float knee_db, float smooth_time = 0.02f);

    /**
     * @brief Get gain reduction in dB (UI only)
     *
     * NOT RT-safe. Must not be used inside audio callback.
     */
    float getGainReduction() const;

private:
    // Parameter smoothing bridge (control → audio thread)
    ParameterSmoother ceiling_smoother_;
    ParameterSmoother release_smoother_;
    ParameterSmoother knee_smoother_;

    /**
     * Audio-thread owned state
     *
     * Ownership rule:
     * Only modified inside process().
     * Never accessed for writing by control thread.
     */

    float envelope_[2]{0.0f, 0.0f};
    // Stereo-linked peak envelope (shared between channels)

    float gain_{1.0f};
    // Current applied gain reduction (linear domain)

    float sample_rate_{48000.0f};

    // Metering (audio → UI thread communication)
    float gain_reduction_db_{0.0f};

    // True-peak oversampling state (not yet active)
    float oversample_prev_[2]{0.0f, 0.0f};

    /**
     * @brief Convert dB to linear amplitude
     * Used only at parameter entry boundaries.
     */
    static float dbToLinear(float db);

    /**
     * @brief Convert linear amplitude to dB
     * For metering only (floored for stability).
     */
    static float linearToDb(float linear);

    /**
     * @brief Compute soft-knee gain reduction
     *
     * RT-Safe DSP core function.
     * Uses quadratic interpolation for smooth limiting curve.
     */
    float computeGain(float input_level, float threshold, float knee);

    /**
     * @brief Oversample by 2x using linear interpolation
     * Not yet wired into process() path.
     */
    void oversample2x(const float* input, float* output,
                      size_t count, float& prev, int channel);

    /**
     * @brief Detect true peak (2x oversampled)
     * Not yet active in processing chain.
     */
    float detectTruePeak(const float* left, const float* right, size_t count);
};

} // namespace RenAmp
