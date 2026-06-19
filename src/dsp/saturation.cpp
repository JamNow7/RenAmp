/*
 * Renamp — Saturation
 * Purpose: asymmetrical soft clip with DC blocking; mix and output gain.
 * Real-time safety: no allocations/locks/I/O in process(); smoothed parameters avoid zipper noise.
 */
#include "dsp/saturation.h"
#include <algorithm>
#include <cmath>

namespace RenaAmp {

Saturation::Saturation() = default;

void Saturation::initialize(float sample_rate) {
    sample_rate_ = sample_rate;

    // Reset filter state
    dc_filter_state_[0] = 0.0f;
    dc_filter_state_[1] = 0.0f;

    // DC blocking filter at ~5 Hz
    // coeff = exp(-2 * pi * fc / fs)
    dc_filter_coeff_ = std::exp(-6.28318530718f * 5.0f / sample_rate_);

    // Initialize parameter smoothers with default values
    drive_smoother_.init(sample_rate, 1.0f);
    asymmetry_smoother_.init(sample_rate, 0.0f);
    dc_offset_smoother_.init(sample_rate, 0.0f);
    mix_smoother_.init(sample_rate, 1.0f);
    output_gain_smoother_.init(sample_rate, 1.0f);
}

void Saturation::process(float* left, float* right, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        // Get smoothed parameter values (one per sample)
        const float drive = drive_smoother_.next();
        const float asymmetry = asymmetry_smoother_.next();
        const float dc_offset_param = dc_offset_smoother_.next();
        const float mix = mix_smoother_.next();
        const float output_gain = output_gain_smoother_.next();

        // Pre-compute gain compensation (drive reduces level)
        const float drive_compensation = 1.0f / std::max(1.0f, drive * 0.5f + 0.5f);

        // === LEFT CHANNEL ===
        float dry_left = left[i];

        // Apply DC offset from parameter
        float sample_left = dry_left + dc_offset_param;

        // Apply asymmetry (different gain for positive/negative)
        sample_left = applyAsymmetry(sample_left, asymmetry);

        // Apply drive
        sample_left *= drive;

        // Soft clipping with fast tanh
        float saturated_left = fastTanh(sample_left);

        // DC blocking filter (remove DC from asymmetry)
        saturated_left = dcBlock(saturated_left, dc_filter_state_[0]);

        // Apply drive compensation and output gain
        saturated_left *= drive_compensation * output_gain;

        // Mix dry/wet
        left[i] = dry_left * (1.0f - mix) + saturated_left * mix;

        // === RIGHT CHANNEL ===
        float dry_right = right[i];

        // Apply DC offset from parameter
        float sample_right = dry_right + dc_offset_param;

        // Apply asymmetry (different gain for positive/negative)
        sample_right = applyAsymmetry(sample_right, asymmetry);

        // Apply drive
        sample_right *= drive;

        // Soft clipping with fast tanh
        float saturated_right = fastTanh(sample_right);

        // DC blocking filter (remove DC from asymmetry)
        saturated_right = dcBlock(saturated_right, dc_filter_state_[1]);

        // Apply drive compensation and output gain
        saturated_right *= drive_compensation * output_gain;

        // Mix dry/wet
        right[i] = dry_right * (1.0f - mix) + saturated_right * mix;
    }
}

void Saturation::setDrive(float drive, float smooth_time) {
    drive_smoother_.setTarget(std::clamp(drive, 0.0f, 10.0f), smooth_time > 0 ? smooth_time : 0.01f);
}

void Saturation::setAsymmetry(float asymmetry, float smooth_time) {
    asymmetry_smoother_.setTarget(std::clamp(asymmetry, -1.0f, 1.0f), smooth_time > 0 ? smooth_time : 0.01f);
}

void Saturation::setDCOffset(float dc_offset, float smooth_time) {
    dc_offset_smoother_.setTarget(std::clamp(dc_offset, -1.0f, 1.0f), smooth_time > 0 ? smooth_time : 0.001f);
}

void Saturation::setMix(float mix, float smooth_time) {
    mix_smoother_.setTarget(std::clamp(mix, 0.0f, 1.0f), smooth_time > 0 ? smooth_time : 0.02f);
}

void Saturation::setOutputGain(float gain, float smooth_time) {
    output_gain_smoother_.setTarget(std::clamp(gain, 0.0f, 2.0f), smooth_time > 0 ? smooth_time : 0.01f);
}

float Saturation::applyAsymmetry(float sample, float asym) {
    // Asymmetry ranges from -1.0 to 1.0
    // -1.0: boost positive, attenuate negative
    //  0.0: symmetric (no change)
    //  1.0: attenuate positive, boost negative

    if (asym < 0.0f) {
        // Negative asymmetry: boost positive half
        float pos_gain = 1.0f - asym;  // asym = -0.5 → pos_gain = 1.5
        float neg_gain = 1.0f + asym;  // asym = -0.5 → neg_gain = 0.5

        if (sample > 0.0f) {
            return sample * pos_gain;
        } else {
            return sample * neg_gain;
        }
    } else {
        // Positive asymmetry: boost negative half
        float pos_gain = 1.0f - asym;  // asym = 0.5 → pos_gain = 0.5
        float neg_gain = 1.0f + asym;  // asym = 0.5 → neg_gain = 1.5

        if (sample > 0.0f) {
            return sample * pos_gain;
        } else {
            return sample * neg_gain;
        }
    }
}

float Saturation::dcBlock(float sample, float& state) {
    // DC blocking filter: high-pass at ~5 Hz
    // y[n] = x[n] - x[n-1] + coeff * y[n-1]
    float output = sample - state;  // Remove DC (state holds previous input)
    state = sample;                  // Store current input for next iteration

    // Apply smoothing (optional, reduces artifacts)
    // output = output * (1.0f - dc_filter_coeff_) + state * dc_filter_coeff_;

    return output;
}

} // namespace RenaAmp
