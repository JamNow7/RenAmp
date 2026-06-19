/*
 * Renamp — Saturation
 * Purpose: Fast waveshaping distortion with tube-like asymmetry and DC compensation.
 * Real-time safety: No allocations, locks, or I/O in process(); safe for audio callback.
 * Threading model: Control thread updates parameters via set* methods; audio thread calls process();
 *                 cross-thread communication via ParameterSmoother (atomic target → smoothed ramp).
 * DSP chain position: Planned as pre-NAM stage (input distortion before model inference).
 *                      Currently not connected to the active signal path pending QA validation.
 */
#pragma once

#include <atomic>
#include "parameters/param_smoother.h"

namespace RenaAmp {

/**
 * @brief Fast waveshaping distortion with tube-like asymmetry
 *
 * Features:
 * - Fast polynomial tanh approximation (~0.0001 error bound, no std::tanh in hot path)
 * - Configurable asymmetry for tube-like rectification (different gain per half-cycle)
 * - DC blocking filter (~5 Hz) to remove offset from asymmetric distortion
 * - Drive compensation to maintain level
 * - Dry/wet mix and output gain controls
 * - Smooth parameter transitions via ParameterSmoother
 *
 * RT-Safe: No allocations, locks, or I/O in process()
 *
 * Note: Currently not wired in DSPChain.process() pending QA validation.
 */
class Saturation {
public:
    Saturation();
    ~Saturation() = default;

    /**
     * @brief Initialize saturation with sample rate
     * @param sample_rate Sample rate in Hz
     * @pre sample_rate > 0
     * @post Saturation ready for process(); DC filter configured at ~5 Hz; parameters can be set
     */
    void initialize(float sample_rate);

    /**
     * @brief Process a block of stereo samples in-place
     * @param left Left channel buffer (size: count)
     * @param right Right channel buffer (size: count)
     * @param count Number of samples per channel
     * @pre Buffers are non-null, 32-bit float
     * @post Audio processed in-place with waveshaping applied
     * @note RT-Safe: callable from audio callback thread
     *
     * Processing pipeline per sample:
     * DC offset → Asymmetry → Drive → fastTanh → DC blocking → Drive compensation + Output gain → Dry/wet mix
     */
    void process(float* left, float* right, size_t count);

    /**
     * @brief Set drive amount (distortion intensity)
     * @param drive Range: 0.0 (clean) to 10.0 (heavy), default 1.0
     * @param smooth_time Smoothing duration in seconds (default: 0.01 = 10ms)
     * @note Thread-safe: can be called from control thread
     */
    void setDrive(float drive, float smooth_time = 0.01f);

    /**
     * @brief Set asymmetry (tube-like rectification)
     * @param asymmetry Range: -1.0 to 1.0 (0.0 = symmetric)
     *                   Negative: boost positive, attenuate negative (tube-like)
     *                   Positive: attenuate positive, boost negative
     * @param smooth_time Smoothing duration in seconds (default: 0.01)
     */
    void setAsymmetry(float asymmetry, float smooth_time = 0.01f);

    /**
     * @brief Set DC offset compensation
     * @param dc_offset Range: -1.0 to 1.0, default 0.0
     * @param smooth_time Smoothing duration in seconds (default: 0.001 = 1ms)
     * @note Adds DC before distortion; internal DC blocking filter removes it after
     */
    void setDCOffset(float dc_offset, float smooth_time = 0.001f);

    /**
     * @brief Set mix (dry/wet)
     * @param mix 0.0 = fully dry (clean), 1.0 = fully wet (distorted), default 1.0
     * @param smooth_time Smoothing duration in seconds (default: 0.02 = 20ms)
     */
    void setMix(float mix, float smooth_time = 0.02f);

    /**
     * @brief Set output gain after saturation
     * @param gain Range: 0.0 to 2.0, default 1.0
     * @param smooth_time Smoothing duration in seconds (default: 0.01)
     * @note Applied after drive compensation to adjust final output level
     */
    void setOutputGain(float gain, float smooth_time = 0.01f);

private:
    // Parameter smoothers (control thread → audio thread safe bridge)
    ParameterSmoother drive_smoother_;
    ParameterSmoother asymmetry_smoother_;
    ParameterSmoother dc_offset_smoother_;
    ParameterSmoother mix_smoother_;
    ParameterSmoother output_gain_smoother_;

    // Audio-thread owned state (never written from control thread)
    float sample_rate_{48000.0f};           ///< Sample rate
    float dc_filter_state_[2]{0.0f, 0.0f};  ///< DC blocking filter state (L, R)
    float dc_filter_coeff_{0.999f};          ///< DC filter coefficient (~5Hz highpass)

    /**
     * @brief Fast polynomial tanh approximation
     * Much faster than std::tanh, accurate to ~0.0001
     * Approximation: tanh(x) ≈ x * (27 + x²) / (27 + 9*x²)
     */
    static inline float fastTanh(float x) {
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /**
     * @brief Apply asymmetric waveshaping
     * Applies different gain to positive/negative half-cycles
     * @param asym -1.0 to 1.0 (negative = boost positive, positive = boost negative)
     */
    inline float applyAsymmetry(float sample, float asym);

    /**
     * @brief DC blocking filter (high-pass at ~5Hz)
     * Removes DC offset introduced by asymmetric distortion
     * Formula: y[n] = x[n] - x[n-1]
     */
    inline float dcBlock(float sample, float& state);
};

} // namespace RenaAmp
