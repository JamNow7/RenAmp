/*
 * RenAmp — CabinetProcessor
 *
 * Purpose:
 * Load and convolve cabinet impulse responses (IRs).
 * Provides post-NAM tonal shaping through IR simulation.
 *
 * Real-time safety:
 * process() is RT-safe:
 * - No allocations
 * - No locks or mutexes
 * - No file I/O
 * - No logging or syscalls
 * - No exception throwing
 *
 * All IR data is preloaded in control thread, normalized, and swapped
 * atomically via lock-free double buffering (active_slot_).
 *
 * Memory ownership:
 * IR and processing buffers are owned exclusively by the control thread
 * during loading phase. After activation, they become read-only from
 * the audio thread.
 *
 * Threading model:
 * - Control/loader thread: loadIR(), setBypass(), setMix()
 * - Audio callback thread: process()
 * - Cross-thread communication: atomic slot swap (active_slot_)
 *
 * Important:
 * Audio thread must never access or modify control-thread-owned memory.
 *
 * Processing model:
 * Block-based audio processing (typical block size: 64–256 samples).
 * Not sample-accurate by design; optimized for deterministic throughput,
 * low latency, and embedded deployment (Raspberry Pi class targets).
 *
 * DSP chain position:
 * Input signal is processed after NAM inference stage and before
 * master gain / output stage.
 *
 * Design constraints:
 * Cabinet IR processing is intentionally limited to short impulse responses
 * representing guitar cabinet behavior (early reflections + resonance body).
 *
 * This is a deliberate trade-off:
 * - Prioritizes CPU efficiency and predictable latency
 * - Avoids FFT partitioning complexity in current version
 * - Optimized for real-time embedded execution
 *
 * Note on IR length:
 * Default IR size (~256 samples) is a performance-driven constraint,
 * not a fidelity limitation. It targets real-time stability on embedded
 * hardware while preserving core tonal character.
 *
 * Future enhancement path:
 * Partitioned FFT convolution for extended IR lengths and higher fidelity
 * room/cabinet simulation when CPU budget allows.
 */
#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include "parameters/param_smoother.h"

namespace RenaAmp {

/**
 * @brief Cabinet Impulse Response Processor
 *
 * RT-Safe:
 * Must not perform allocations, locks, I/O, logging, or syscalls
 * inside process() or any audio-thread function.
 *
 * Features:
 * - Convolution with impulse responses (IRs)
 * - Double buffering for RT-safe IR swaps
 * - WAV IR loading (control thread only)
 * - Gain staging post-NAM
 * - Bypass switch
 * - Smooth dry/wet transitions via ParameterSmoother
 *
 * Processing is optimized for short IRs (< 2048 samples),
 * typical of guitar cabinet modeling workloads.
 */
class CabinetProcessor {
public:
    CabinetProcessor();
    ~CabinetProcessor();

    /**
     * @brief Initialize processor with sample rate
     * @param sample_rate Audio sample rate in Hz
     *
     * Precondition: Must be called from control thread before audio start.
     * Postcondition: Processor is ready for real-time processing.
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
     *
     * Thread: Audio callback thread only
     */
    void process(float* left, float* right, size_t count);

    /**
     * @brief Load IR from .wav file (control thread only)
     * @return true if loading succeeded
     *
     * Thread-safety:
     * Uses double-buffered swap; audio thread always reads active slot.
     */
    bool loadIR(const std::string& filepath);

    /**
     * @brief Enable/disable cabinet simulation
     * Thread-safe (control thread only)
     */
    void setBypass(bool bypass);

    /**
     * @brief Set dry/wet mix with smoothing
     * @param mix 0.0 = dry, 1.0 = wet
     * @param smooth_time smoothing time in seconds (default 50ms)
     */
    void setMix(float mix, float smooth_time = 0.05f);

    /**
     * @brief Check if IR is loaded and active
     */
    bool isIRLoaded() const;

    /**
     * @brief Get active IR length in samples
     */
    size_t getIRLength() const;

private:
    /**
     * IR Slot (double-buffered ownership model)
     *
     * Ownership:
     * - Written by control thread during loadIR()
     * - Read-only access from audio thread after activation
     *
     * RT constraint:
     * Must never be modified from audio thread.
     */
    struct IRSlot {
        std::vector<float> ir_left;
        std::vector<float> ir_right;

        // Circular history buffers (preallocated, fixed size)
        std::vector<float> history_left;
        std::vector<float> history_right;

        size_t write_pos_left{0};
        size_t write_pos_right{0};

        std::atomic<bool> ready{false};
        std::atomic<bool> active{false};
    };

    // Double buffer slots (ownership swapped atomically)
    std::unique_ptr<IRSlot> slots_[2];
    std::atomic<int> active_slot_{0};

    // Parameters (control-thread written, audio-thread read)
    std::atomic<bool> bypass_{true};

    ParameterSmoother mix_smoother_;

    // Preallocated dry buffers (RT-safe, never resized after init)
    std::vector<float> temp_dry_left_;
    std::vector<float> temp_dry_right_;

    // State
    float sample_rate_{48000.0f};
    size_t max_ir_length_{256};

    /**
     * @brief Load WAV file (control thread only)
     */
    bool loadWAV(const std::string& filepath,
                 std::vector<float>& left,
                 std::vector<float>& right);

    /**
     * @brief Process single channel convolution (RT-safe)
     */
    void processChannel(float* output,
                        const float* input,
                        size_t count,
                        const float* ir,
                        size_t ir_length,
                        float* history,
                        size_t& write_pos);

    /**
     * @brief Normalize IR to prevent clipping (control thread only)
     */
    void normalizeIR(std::vector<float>& ir);

    /**
     * @brief Atomic slot swap at block boundary (control thread)
     */
    void swapIRSlot();
};

} // namespace RenaAmp
