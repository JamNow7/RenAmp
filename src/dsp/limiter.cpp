/*
 * Renamp — Limiter
 * Purpose: peak limiter with soft knee; optional stage to prevent inter-sample peaks.
 * Real-time safety: no allocations/locks/I/O in process(); fixed-time gain smoothing independent of detector.
 * Threading: control thread updates smoothed ceiling/release/knee; audio thread applies gain.
 */
#include "dsp/limiter.h"
#include <algorithm>
#include <cmath>

namespace RenaAmp {

Limiter::Limiter() = default;

void Limiter::initialize(float sample_rate) {
    sample_rate_ = sample_rate;

    // Reset state
    envelope_[0] = 0.0f;
    envelope_[1] = 0.0f;
    gain_ = 1.0f;
    gain_reduction_db_ = 0.0f;

    // Reset oversampling state
    oversample_prev_[0] = 0.0f;
    oversample_prev_[1] = 0.0f;

    // Initialize parameter smoothers with default values
    ceiling_smoother_.init(sample_rate, -2.5f);
    release_smoother_.init(sample_rate, 10.0f);
    knee_smoother_.init(sample_rate, 3.0f);
}

void Limiter::process(float* left, float* right, size_t count) {
    for (size_t i = 0; i < count; ++i) {

        // Parameters
        const float ceiling_db = ceiling_smoother_.next();
        const float release_ms = release_smoother_.next();
        const float knee_db = knee_smoother_.next();

        const float ceiling_linear = dbToLinear(ceiling_db);

        // Envelope release coefficient (for detector only)
        const float release_seconds = release_ms / 1000.0f;
        const float release_coeff =
            std::exp(-1.0f / (std::max(release_seconds, 0.0001f) * sample_rate_));

        // Input peak
        float sample_max = std::max(std::fabs(left[i]), std::fabs(right[i]));

        // Envelope follower (peak detector)
        if (sample_max > envelope_[0]) {
            envelope_[0] = sample_max;
            envelope_[1] = sample_max;
        } else {
            envelope_[0] += release_coeff * (sample_max - envelope_[0]);
            envelope_[1] = envelope_[0];
        }

        // Gain computation
        float target_gain = computeGain(envelope_[0], ceiling_linear, knee_db);

        // =========================================================
        // Design note: gain smoothing (independent from envelope)
        // =========================================================
        const float gain_smoothing_coeff =
            std::exp(-1.0f / (0.005f * sample_rate_)); // 5ms fixed smoothing

        gain_ += gain_smoothing_coeff * (target_gain - gain_);

        // Only lower clamp (avoid hard pumping artifacts)
        gain_ = std::max(0.0f, gain_);

        // Apply gain
        left[i] *= gain_;
        right[i] *= gain_;

        // Metering
        if (gain_ < 0.999f) {
            gain_reduction_db_ = linearToDb(gain_);
        } else {
            gain_reduction_db_ = 0.0f;
        }
    }
}

void Limiter::setCeiling(float ceiling_db, float smooth_time) {
    ceiling_smoother_.setTarget(
        std::clamp(ceiling_db, -20.0f, 0.0f),
        smooth_time > 0 ? smooth_time : 0.02f
    );
}

void Limiter::setRelease(float release_ms, float smooth_time) {
    release_smoother_.setTarget(
        std::clamp(release_ms, 1.0f, 100.0f),
        smooth_time > 0 ? smooth_time : 0.01f
    );
}

void Limiter::setKnee(float knee_db, float smooth_time) {
    knee_smoother_.setTarget(
        std::clamp(knee_db, 0.0f, 6.0f),
        smooth_time > 0 ? smooth_time : 0.02f
    );
}

float Limiter::getGainReduction() const {
    return gain_reduction_db_;
}

float Limiter::dbToLinear(float db) {
    return std::exp(0.1151292546497022842f * db);
}

float Limiter::linearToDb(float linear) {
    const float epsilon = 1e-10f;
    if (linear < epsilon) return -60.0f;
    return 20.0f * std::log10(linear);
}

/*
 * TODO(owner:ccataldo, 2026-06-18): Review threshold units in computeGain().
 * Caller passes ceiling_linear (already linear) from process(), but this function
 * converts 'threshold' again via dbToLinear(), which suggests a double-conversion.
 * Decide whether computeGain() should receive dB and convert here, or keep linear
 * and remove the extra conversion. No behavior change now (comment-only).
 */
float Limiter::computeGain(float input_level, float threshold, float knee) {
    // ========================================================================
    // SOFT-KNEE LIMITING GAIN COMPUTATION
    // ========================================================================
    // Three regions:
    // 1. Below knee: unity gain (no limiting)
    // 2. Knee region: quadratic interpolation (smooth transition)
    // 3. Above knee: hard limiting (gain = threshold / input)
    //
    // NOTE: Parameter 'threshold' is in dB, converted to linear here.
    //       See TODO about potential double-conversion issue.
    // ========================================================================

    const float knee_half = knee * 0.5f;            // Half knee width (for symmetric bounds)
    const float threshold_linear = dbToLinear(threshold);  // Ceiling in linear

    // Knee boundaries: [threshold - knee/2, threshold + knee/2] in linear domain
    const float knee_lower = threshold_linear * dbToLinear(-knee_half);
    const float knee_upper = threshold_linear * dbToLinear(knee_half);

    // Region 1: Below knee (no limiting)
    if (input_level <= knee_lower) {
        return 1.0f;  // Unity gain
    }

    // Region 3: Above knee (hard limiting)
    if (input_level >= knee_upper) {
        float gain = threshold_linear / input_level;  // Brickwall gain reduction
        return std::max(0.0f, gain);  // Clamp to non-negative
    }

    // Region 2: Soft-knee (quadratic interpolation for smooth transition)
    float normalized = (input_level - knee_lower) / (knee_upper - knee_lower);  // 0 to 1

    float gain_at_lower = 1.0f;                                 // Unity gain at knee start
    float gain_at_upper = threshold_linear / knee_upper;         // Target gain at knee end

    // Quadratic: gain(t) = 1 + (g_target - 1) × t²  (t = normalized position)
    float gain = gain_at_lower + (gain_at_upper - gain_at_lower) * normalized * normalized;

    return std::max(0.0f, gain);
}

float Limiter::detectTruePeak(const float* left, const float* right, size_t count) {
    float peak = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        peak = std::max({
            peak,
            std::fabs(left[i]),
            std::fabs(right[i])
        });
    }

    return peak;
}

void Limiter::oversample2x(const float* input, float* output,
                          size_t count, float& prev, int channel) {
    for (size_t i = 0; i < count; ++i) {
        output[2 * i]     = (prev + input[i]) * 0.5f;
        output[2 * i + 1] = input[i];
        prev = input[i];
    }
}

} // namespace RenaAmp