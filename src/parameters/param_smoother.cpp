/*
 * Renamp — ParameterSmoother
 * Purpose: cross-thread parameter ramping to avoid zipper noise.
 * Threading: control thread sets targets; audio thread consumes next(); lock-free handoff via atomics.
 */
#include "parameters/param_smoother.h"
#include <algorithm>

namespace RenaAmp {

// ============================================================================
// ParameterSmoother Implementation
// ============================================================================
// Design: Linear ramp from current value to target over N samples.
// Why: Prevents zipper noise (audible stepping) when control thread updates parameters.
// Threading: Lock-free atomic flag signals pending update; audio thread processes updates
//            between samples without blocking. No locks or allocations in next().
// ============================================================================

ParameterSmoother::ParameterSmoother() = default;

void ParameterSmoother::init(float sample_rate, float initial_value) {
    sample_rate_ = (sample_rate > 0.0f ? sample_rate : 48000.0f);
    current_ = initial_value;
    target_ = initial_value;
    step_ = 0.0f;
    samples_remaining_ = 0;
    pending_update_.store(false, std::memory_order_relaxed);
}

void ParameterSmoother::setTarget(float target, float duration_seconds) {
    // Store update in atomic structure
    pending_update_data_.target = target;
    pending_update_data_.duration_seconds = duration_seconds;
    pending_update_.store(true, std::memory_order_release);
}

float ParameterSmoother::next() {
    // Check for pending update from control thread
    if (pending_update_.load(std::memory_order_acquire)) {
        processPendingUpdate();
    }

    // If smoothing is active, advance toward target
    if (samples_remaining_ > 0) {
        current_ += step_;
        samples_remaining_--;

        // Clamp to target to prevent overshoot
        if (samples_remaining_ == 0) {
            current_ = target_;
        }
    }

    return current_;
}

float ParameterSmoother::getCurrent() const {
    return current_;
}

void ParameterSmoother::reset(float value) {
    current_ = value;
    target_ = value;
    step_ = 0.0f;
    samples_remaining_ = 0;
    pending_update_.store(false, std::memory_order_relaxed);
}

bool ParameterSmoother::isSmoothing() const {
    return samples_remaining_ > 0;
}

void ParameterSmoother::processPendingUpdate() {
    TargetUpdate update = pending_update_data_;

    float sr = (sample_rate_ > 0.0f ? sample_rate_ : 48000.0f);
    float duration_samples = update.duration_seconds * sr;
    int32_t samples = static_cast<int32_t>(duration_samples + 0.5f);

    if (samples < 1) {
        current_ = update.target;
        target_ = update.target;
        step_ = 0.0f;
        samples_remaining_ = 0;
    } else {
        target_ = update.target;
        step_ = calculateStep(current_, target_, static_cast<float>(samples));
        samples_remaining_ = samples;
    }

    pending_update_.store(false, std::memory_order_relaxed);
}

float ParameterSmoother::calculateStep(float from, float to, float duration_samples) {
    if (duration_samples < 1.0f) {
        return 0.0f;
    }

    // Linear ramp: step = (target - current) / duration.
    // Why linear: simple, deterministic, and sufficient for most audio parameters.
    // For perceptual parameters (e.g. frequency), consider exponential curves instead.
    return (to - from) / duration_samples;
}

// ============================================================================
// MultiParameterSmoother Implementation
// ============================================================================

MultiParameterSmoother::MultiParameterSmoother() = default;

void MultiParameterSmoother::init(float sample_rate, size_t num_channels, float initial_value) {
    num_channels_ = std::min(num_channels, MAX_CHANNELS);

    for (size_t i = 0; i < num_channels_; ++i) {
        smoothers_[i].init(sample_rate, initial_value);
        values_[i] = initial_value;
    }
}

void MultiParameterSmoother::setTargetAll(float target, float duration_seconds) {
    for (size_t i = 0; i < num_channels_; ++i) {
        smoothers_[i].setTarget(target, duration_seconds);
    }
}

void MultiParameterSmoother::setTargetChannel(size_t channel, float target, float duration_seconds) {
    if (channel < num_channels_) {
        smoothers_[channel].setTarget(target, duration_seconds);
    }
}

const float* MultiParameterSmoother::next() {
    for (size_t i = 0; i < num_channels_; ++i) {
        values_[i] = smoothers_[i].next();
    }
    return values_;
}

float MultiParameterSmoother::getChannel(size_t channel) const {
    if (channel < num_channels_) {
        return smoothers_[channel].getCurrent();
    }
    return 0.0f;
}

void MultiParameterSmoother::resetAll(float value) {
    for (size_t i = 0; i < num_channels_; ++i) {
        smoothers_[i].reset(value);
        values_[i] = value;
    }
}

} // namespace RenaAmp
