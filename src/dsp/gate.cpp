/*
 * Renamp — Gate
 * Purpose: stereo-linked gate with hysteresis and smoothed parameters.
 * Real-time safety: no allocations/locks/I/O in process(); per-sample coeffs derived from smoothed controls.
 * Threading: control thread sets targets via ParameterSmoother; audio thread consumes via next().
 */
#include "dsp/gate.h"
#include <cmath>
#include <algorithm>

namespace RenaAmp {

Gate::Gate() = default;

void Gate::initialize(float sample_rate) {
    sample_rate_ = sample_rate;
    envelope_ = 0.0f;
    current_gain_ = 1.0f;
    gate_open_ = true;
    gain_reduction_db_ = 0.0f;

    // Initialize parameter smoothers with default values
    threshold_smoother_.init(sample_rate, -40.0f);
    attack_smoother_.init(sample_rate, 1.0f);
    release_smoother_.init(sample_rate, 50.0f);
    hysteresis_smoother_.init(sample_rate, 3.0f);

    updateCoefficients();
}

void Gate::process(float* left, float* right, size_t count) {
    // Update coefficients at block start (pick up parameter changes)
    updateCoefficients();

    for (size_t i = 0; i < count; ++i) {
        // Get smoothed parameter values (one per sample)
        const float threshold_db = threshold_smoother_.next();
        const float attack_ms = attack_smoother_.next();
        const float release_ms = release_smoother_.next();
        const float hysteresis_db = hysteresis_smoother_.next();

        // Update coefficients based on smoothed parameters
        const float attack_seconds = attack_ms / 1000.0f;
        const float release_seconds = release_ms / 1000.0f;
        attack_coeff_ = std::exp(-1.0f / (attack_seconds * sample_rate_));
        release_coeff_ = std::exp(-1.0f / (release_seconds * sample_rate_));

        const float threshold_linear = dbToLinear(threshold_db);
        const float hysteresis_linear = dbToLinear(hysteresis_db);
        const float open_threshold = threshold_linear;
        const float close_threshold = threshold_linear * hysteresis_linear;

        // Stereo-linked envelope: use max of left/right
        float sample_max = std::max(std::fabs(left[i]), std::fabs(right[i]));

        // Envelope follower (peak detector with smoothing)
        if (sample_max > envelope_) {
            // Attack phase
            envelope_ += attack_coeff_ * (sample_max - envelope_);
        } else {
            // Release phase
            envelope_ += release_coeff_ * (sample_max - envelope_);
        }

        // Gate logic with hysteresis
        if (gate_open_) {
            // Gate is open, check if we should close
            if (envelope_ < close_threshold) {
                gate_open_ = false;
            }
        } else {
            // Gate is closed, check if we should open
            if (envelope_ > open_threshold) {
                gate_open_ = true;
            }
        }

        // Smooth gain transition to avoid clicks
        float target_gain = gate_open_ ? 1.0f : 0.0f;
        const float gain_smoothing = gate_open_ ? attack_coeff_ : release_coeff_;
        current_gain_ += gain_smoothing * (target_gain - current_gain_);

        // Apply gain
        left[i] *= current_gain_;
        right[i] *= current_gain_;

        // Update metering
        if (current_gain_ < 0.999f) {
            gain_reduction_db_ = linearToDb(current_gain_);
        } else {
            gain_reduction_db_ = 0.0f;
        }
    }
}

void Gate::setThreshold(float threshold_db, float smooth_time) {
    // Use default smoothing time if not specified
    if (smooth_time <= 0.0f) {
        smooth_time = 0.01f;  // 10ms default
    }
    threshold_smoother_.setTarget(std::clamp(threshold_db, -60.0f, 0.0f), smooth_time);
}

void Gate::setAttack(float attack_ms) {
    attack_smoother_.setTarget(std::clamp(attack_ms, 0.1f, 100.0f), 0.01f);
}

void Gate::setRelease(float release_ms) {
    release_smoother_.setTarget(std::clamp(release_ms, 1.0f, 1000.0f), 0.01f);
}

void Gate::setHysteresis(float hysteresis_db) {
    hysteresis_smoother_.setTarget(std::clamp(hysteresis_db, 0.0f, 12.0f), 0.01f);
}

bool Gate::isOpen() const {
    return gate_open_;
}

float Gate::getGainReduction() const {
    return gain_reduction_db_;
}

float Gate::dbToLinear(float db) {
    return std::exp(0.1151292546497022842f * db);  // log(10)/20
}

float Gate::linearToDb(float linear) {
    const float epsilon = 1e-10f;
    if (linear < epsilon) {
        return -60.0f;  // Floor
    }
    return 20.0f * std::log10(linear);
}

void Gate::updateCoefficients() {
    // Coefficients are now calculated per-sample in process()
    // using smoothed parameter values from ParameterSmoother
    // This method is kept for compatibility but does nothing
}

} // namespace RenaAmp
