/*
 * Renamp — DSP Chain (implementation)
 * Purpose: Implements the audio processing path for DSPChain.
 * Real-time safety: No dynamic allocations, no locks, and no I/O inside process();
 *                  parameter updates occur from the control thread via atomics.
 * Threading model: Control thread sets parameters; audio thread calls process().
 * Runtime order (2026-06-17): NAM → Cabinet → Master Gain → Limiter.
 * Debug metering: DSPMeter is development-only and should not run on the audio callback path.
 */
#include "dsp/dsp_chain.h"
#include <cmath>
#include <iostream>
#include <algorithm>

namespace RenaAmp {

// Debug-only DSP metering (development tool, not for the audio callback).
// Purpose: verify headroom, RMS and crest factor during development sessions.
// Note: keep out of the audio callback hot path; wrap under a compile-time flag if needed.

struct DSPMeter {
    float peak = 0.0f;
    float rms_sum = 0.0f;
    size_t rms_count = 0;
    size_t near_clip_count = 0;
    size_t buffer_count = 0;

    static constexpr float NEAR_CLIP_THRESHOLD = 0.99f;
    static constexpr size_t LOG_INTERVAL = 1000;
    static constexpr float EPSILON = 1e-10f;

    void reset() {
        peak = 0.0f;
        rms_sum = 0.0f;
        rms_count = 0;
        near_clip_count = 0;
        buffer_count = 0;
    }

    void process(const float* left, const float* right, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            float l = left[i];
            float r = right[i];
            float sample_max = std::max(std::fabs(l), std::fabs(r));
            peak = std::max(peak, sample_max);
            rms_sum += 0.5f * (l * l + r * r);
            rms_count++;
            if (sample_max > NEAR_CLIP_THRESHOLD) {
                near_clip_count++;
            }
        }
        buffer_count++;
    }

    float getPeak_dBFS() const {
        return 20.0f * std::log10(std::max(peak, EPSILON));
    }

    float getRMS_dBFS() const {
        if (rms_count == 0) return -60.0f;
        float rms = std::sqrt(rms_sum / static_cast<float>(rms_count));
        return 20.0f * std::log10(std::max(rms, EPSILON));
    }

    float getCrestFactor_dB() const {
        if (rms_count == 0) return 0.0f;
        float rms = std::sqrt(rms_sum / static_cast<float>(rms_count));
        if (rms < EPSILON) return 0.0f;
        return 20.0f * std::log10(peak / rms);
    }

    bool shouldLog() const {
        return buffer_count >= LOG_INTERVAL;
    }

    void log() const {
        std::cout << "\n=== RenaAmp DSP Meter ===" << std::endl;
        std::cout << "Peak:     " << getPeak_dBFS() << " dBFS" << std::endl;
        std::cout << "RMS:      " << getRMS_dBFS() << " dBFS" << std::endl;
        std::cout << "Crest:    " << getCrestFactor_dB() << " dB" << std::endl;
        std::cout << "NearClip: " << near_clip_count << " samples" << std::endl;

        if (peak > 0.999f) {
            std::cout << "STATUS:   CLIPPING DETECTED!" << std::endl;
        } else if (near_clip_count > 0) {
            std::cout << "STATUS:   Near clipping detected" << std::endl;
        } else if (getRMS_dBFS() > -6.0f) {
            std::cout << "STATUS:   High RMS (hot signal)" << std::endl;
        } else {
            std::cout << "STATUS:   OK" << std::endl;
        }

        std::cout << "==========================" << std::endl;
    }
};

// ============================================================================
// DSPChain Implementation
// ============================================================================

void DSPChain::initialize(float sample_rate) {
    // Initialize all DSP components even if some stages are not currently wired.
    // Why: keeps parameter surfaces stable and allows enabling stages later without
    // reallocations or threading hazards (important for RT-safe hot routing).
    gate_.initialize(sample_rate);
    saturation_.initialize(sample_rate);
    nam_.initialize(sample_rate);
    cabinet_.initialize(sample_rate);
    limiter_.initialize(sample_rate);

    // Initialize master gain to 0 dB.
    // Why: expose a post-NAM/IR loudness control that preserves the model/IR spectral balance.
    // RT note: the smoother avoids zipper noise from control-thread parameter updates.
    master_gain_db_.init(sample_rate, 0.0f);

    // Limiter is optional and disabled by default.
    // Why: guitar-amp workflows favor natural dynamics and soft saturation from the model;
    // enable when interfacing with line-level gear or to prevent inter-sample peaks.
    limiter_enabled_ = false;
}

void DSPChain::process(float* left, float* right, size_t count) {
    nam_.process(left, right, count);
    cabinet_.process(left, right, count);

    // Apply master gain (dB smoothed) before limiter
    // Master gain is applied after NAM and Cabinet to avoid re-exciting the model/IR
    // when changing loudness; preserves perceived tone.
    // 0.1151292546497022842f = ln(10)/20 (dB → linear).
    const float gain_db = master_gain_db_.next();
    const float gain = std::exp(0.1151292546497022842f * gain_db); // dbToLinear
    for (size_t i = 0; i < count; ++i) {
        left[i]  *= gain;
        right[i] *= gain;
    }

    if (limiter_enabled_) {
        limiter_.process(left, right, count);
    }

    // Final safety clamp to [-1, 1] to prevent out-of-range samples when the limiter is disabled;
    // cheap and deterministic; not a substitute for a dynamics processor.
    for (size_t i = 0; i < count; ++i) {
        left[i]  = std::clamp(left[i], -1.0f, 1.0f);
        right[i] = std::clamp(right[i], -1.0f, 1.0f);
    }
}

void DSPChain::setMasterGain(float gain_db, float smooth_time) {
    master_gain_db_.setTarget(std::clamp(gain_db, -20.0f, 20.0f), smooth_time > 0 ? smooth_time : 0.02f);
}

} // namespace RenaAmp