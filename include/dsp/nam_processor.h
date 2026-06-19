/*
 * RenAmp — NAMProcessor
 *
 * Purpose:
 * Real-time neural amp modeler inference engine.
 * Runs LSTM or WaveNet models for guitar amp simulation,
 * with wet/dry mixing, gain staging, and DC blocking.
 *
 * Real-time safety:
 * process() is RT-safe:
 * - No allocations
 * - No locks or mutexes
 * - No file I/O
 * - No logging or syscalls
 * - No exception throwing
 *
 * Model loading (setModel) runs on the control/loader thread and
 * performs allocations. Audio thread never triggers loading.
 *
 * All WaveNet/LSTM processing buffers are pre-allocated at initialize()
 * and never resized during process(). Denormal protection uses tiny-noise
 * injection (no FTZ/DAZ CPU flags required).
 *
 * Memory ownership:
 * Model weights are owned by NAMModel (external lifetime must outlive slot use).
 * LSTM state vectors and WaveNet DSP objects are owned by ModelSlot.
 * Processing temp buffers (dry/wet/mix) are owned by NAMProcessor,
 * pre-allocated at init, never resized in the hot path.
 *
 * Threading model:
 * - Control/loader thread: setModel(), setBypass(), setInputGain(),
 *   setOutputGain(), setMix()
 * - Audio callback thread: process()
 * - Cross-thread communication:
 *     - ParameterSmoother for gain/mix (atomic target → smooth ramp)
 *     - Double-buffer slot swap: inactive slot written by control thread,
 *       then activated via atomic store with release semantics.
 *       Audio thread reads active_slot_ with acquire semantics.
 *
 * Processing model:
 * Per-sample processing for LSTM (manual unrolled inference loop).
 * Block-based processing for WaveNet (via NeuralAmpModelerCore DSP).
 * Both paths produce mono output duplicated to stereo channels.
 *
 * Denormal protection:
 * Tiny alternating noise (±1e-20) is injected into the input signal
 * before neural inference to prevent denormal stalls in LSTM/GRU layers
 * during deep silence. This is a portable alternative to FTZ/DAZ
 * hardware flags and has no audible effect on the signal.
 *
 * DSP chain position:
 * First processing stage after input. Receives raw guitar signal and
 * produces modeled amp output. Followed by Cabinet IR.
 * See dsp_chain for full chain order.
 *
 * Design decisions:
 * Two inference backends are supported:
 * 1. Native LSTM: manual forward-pass implementation with fast approximations
 *    (fastTanh via rational polynomial, fastSigmoid via fastTanh). Avoids
 *    dependency on external ML frameworks at runtime.
 * 2. WaveNet: delegates to NeuralAmpModelerCore's DSP class, which uses
 *    double-precision internally. Requires pre-conversion of input to double.
 *
 * This is a deliberate trade-off:
 * - Native LSTM gives deterministic, low-overhead inference on embedded targets
 * - WaveNet support provides compatibility with the broader NAM model ecosystem
 * - Double conversion for WaveNet (float→double→float) adds CPU cost but
 *   matches NeuralAmpModelerCore's internal precision
 *
 * DC blocking:
 * A first-order high-pass filter (coefficient ~0.995 at 48 kHz) removes
 * low-frequency drift and DC offset from model output. Applied only to
 * the LSTM path (WaveNet uses NeuralAmpModelerCore's internal handling).
 *
 * Known constraints:
 * - MAX_BUFFER_SIZE (4096) caps the maximum JACK block size supported.
 *   Blocks larger than this are silently dropped (process() returns early).
 * - WaveNet models are significantly heavier than LSTM on CPU.
 *   Release builds and 64–128 frame buffers are recommended.
 * - Model pointer lifetime: the caller must ensure NAMModel outlives
 *   the ModelSlot using it. No reference counting is performed.
 *
 * Future enhancement path:
 * - Variable buffer size support (internal sub-block splitting)
 * - Optimized LSTM with SIMD matrix operations
 * - Batch processing mode for WaveNet
 * - Model hot-swap with cross-fade (current swap is instant)
 * - FTZ/DAZ hardware flag detection as supplement to noise guard
 */
#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include "parameters/param_smoother.h"
#include "models/nam_model.h"

// Forward declaration for NeuralAmpModelerCore DSP (WaveNet support)
namespace nam {
    class DSP;
}

namespace RenaAmp {

/**
 * @brief Neural Amp Modeler real-time inference engine
 *
 * RT-Safe:
 * Must not perform allocations, locks, I/O, logging, or syscalls
 * inside process() or any audio-thread function.
 *
 * Model loading (setModel) is NOT RT-safe — must be called from
 * control/loader thread only.
 *
 * Info methods (isModelLoaded, getModelInfo) are NOT RT-critical
 * and must only be used from UI / control thread.
 *
 * Features:
 * - Dual-backend inference: native LSTM or NeuralAmpModelerCore WaveNet
 * - Double-buffered model swapping (zero-click preset changes)
 * - Input/output gain staging with smooth transitions
 * - Dry/wet mix for parallel processing
 * - Denormal protection via tiny-noise injection
 * - DC blocking on LSTM output path
 * - ARM NEON SIMD optimization for wet/dry mixing
 */
class NAMProcessor {
public:
    NAMProcessor();
    ~NAMProcessor();

    /**
     * @brief Initialize processor with sample rate
     * @param sample_rate Audio sample rate in Hz
     *
     * Precondition: Must be called from control thread before audio start.
     * Postcondition: Buffers allocated, smoothers initialized, ready for processing.
     *
     * @warning NOT RT-safe — allocates processing buffers.
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
     *
     * Block sizes exceeding MAX_BUFFER_SIZE are silently dropped.
     */
    void process(float* left, float* right, size_t count);

    /**
     * @brief Load a NAM model into the inactive slot
     * @param model Pointer to NAMModel (caller must ensure lifetime)
     * @param buffer_size JACK buffer size (for WaveNet Reset)
     * @return true if model was loaded successfully
     *
     * NOT RT-safe — allocates WaveNet DSP, copies LSTM state.
     * Thread: Control/loader thread only.
     *
     * Uses double-buffer swap: inactive slot is prepared first,
     * then atomically activated at block boundary.
     */
    bool setModel(const NAMModel* model, int buffer_size = 64);

    /**
     * @brief Enable/disable NAM processing
     * Thread: Control thread only (atomic store)
     */
    void setBypass(bool bypass);

    /**
     * @brief Set input gain (applied before neural network)
     * @param gain_db Range: -20 dB to +20 dB, default 0 dB
     */
    void setInputGain(float gain_db, float smooth_time = 0.01f);

    /**
     * @brief Set output gain (applied after neural network)
     * @param gain_db Range: -20 dB to +20 dB, default 0 dB
     */
    void setOutputGain(float gain_db, float smooth_time = 0.01f);

    /**
     * @brief Set dry/wet mix
     * @param mix 0.0 = fully dry, 1.0 = fully wet
     */
    void setMix(float mix, float smooth_time = 0.05f);

    /**
     * @brief Check if a model is loaded and active (UI only)
     *
     * NOT RT-safe for audio processing usage.
     */
    bool isModelLoaded() const;

    /**
     * @brief Get metadata of the active model (UI only)
     *
     * NOT RT-safe for audio processing usage.
     * Returns nullptr if no model is loaded.
     */
    const NAMMetadata* getModelInfo() const;

private:
    /**
     * ModelSlot (double-buffered ownership model)
     *
     * Ownership:
     * - Written by control/loader thread during setModel()
     * - Read-only access from audio thread after activation
     *
     * RT constraint:
     * Must never be modified from audio thread.
     */
    struct ModelSlot {
        const NAMModel* model{nullptr};

        // LSTM weight pointers (point into NAMModel's weights_buffer_)
        // Lifetime: must match the NAMModel that provided them
        const float* weights{nullptr};
        size_t weights_offset{0};

        // LSTM state vectors (persistent across callbacks)
        // Owned by ModelSlot; allocated in setModel(), read/written in process()
        std::vector<float> lstm_state_h;  // Hidden state [num_layers * hidden_size]
        std::vector<float> lstm_state_c;  // Cell state [num_layers * hidden_size]

        // DC blocking filter (removes DC offset from LSTM output)
        float dc_block_x_prev{0.0f};      // Previous input sample
        float dc_block_y_prev{0.0f};      // Previous output sample

        // Configuration cache (populated in setModel)
        int num_layers{0};
        int hidden_size{0};
        bool use_wavenet{false};

        // NeuralAmpModelerCore DSP instance (WaveNet only)
        // Owned by ModelSlot; allocated in setModel()
        std::unique_ptr<nam::DSP> nam_dsp;

        std::atomic<bool> ready{false};
        std::atomic<bool> active{false};
    };

    // Double buffer slots (ownership swapped atomically on setModel)
    std::unique_ptr<ModelSlot> slots_[2];
    std::atomic<int> active_slot_{0};

    // Parameter smoothers (control → audio thread safe bridge)
    std::atomic<bool> bypass_{true};     // Start bypassed until model loaded
    ParameterSmoother input_gain_smoother_;
    ParameterSmoother output_gain_smoother_;
    ParameterSmoother mix_smoother_;

    // Audio-thread owned state
    float sample_rate_{48000.0f};

    // Preallocated WaveNet buffers (never resized after initialize)
    // Why double: NeuralAmpModelerCore operates in double precision internally
    static constexpr size_t MAX_BUFFER_SIZE = 4096;
    std::vector<double> wavenet_input_buffer_;
    std::vector<double> wavenet_output_buffer_;

    // Preallocated mixing buffers (never resized after initialize)
    std::vector<float> temp_dry_left_;
    std::vector<float> temp_dry_right_;
    std::vector<float> temp_wet_left_;
    std::vector<float> temp_wet_right_;

    // === Inference methods (RT-safe, audio thread only) ===

    /**
     * @brief Process LSTM model on a stereo block
     * Delegates to lstmForwardPass per sample, duplicates mono output to stereo.
     */
    void processLSTM(float* left, float* right, size_t count, ModelSlot& slot);

    /**
     * @brief Single-sample LSTM forward pass
     * Manual unrolled inference through all layers with DC blocking.
     *
     * Uses fast approximations (fastTanh, fastSigmoid) for gate activations.
     * DC blocker (R=0.995) removes model output offset.
     */
    float lstmForwardPass(float x, ModelSlot& slot);

    // === Fast math approximations (RT-safe) ===

    /**
     * @brief Fast tanh approximation (rational polynomial, Padé-like)
     * x * (27 + x²) / (27 + 9x²) — max error ~0.01 for |x| < 3.
     */
    static inline float fastTanh(float x);

    /**
     * @brief Fast sigmoid via fastTanh: 0.5 + 0.5 * tanh(x/2)
     */
    static inline float fastSigmoid(float x);

    /**
     * @brief Convert dB to linear amplitude
     * 0.1151292546 = ln(10)/20 (dB → linear)
     */
    float dbToLinear(float db) const;
};

} // namespace RenaAmp
