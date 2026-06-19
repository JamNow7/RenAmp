/*
 * Renamp — RTNeural Wrapper
 * Purpose: Thin wrapper around RTNeural runtime model for NAM model inference.
 * Real-time safety: process() and reset() are RT-safe (no allocations, locks, or I/O).
 *                  initialize() is NOT RT-safe and must run off the audio thread.
 * Threading model: Loader/control thread calls initialize(); audio thread calls process()/reset().
 *                 Cross-thread communication via atomic ready flag.
 * Status: Partially implemented. Structure works; weight loading is TODO pending integration.
 */
#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include "models/nam_model.h"

namespace RenaAmp {

/**
 * @brief RTNeural wrapper for real-time neural network inference
 *
 * Wraps RTNeural library for NAM model inference with RT-safe processing.
 * Supports LSTM architecture (covers ~99.5% of NAM models).
 *
 * Design:
 * - PIMPL pattern to avoid heavy RTNeural includes in header
 * - Pre-allocates all memory during initialization
 * - Lock-free inference via atomic ready flag
 *
 * Performance notes:
 * - Eigen backend: Best for larger networks (hidden_size > 64)
 * - Runtime API: Slightly slower than compile-time but more flexible
 *
 * Note: Currently process() returns passthrough; weight loading is TODO.
 *       See implementation for placeholder status.
 */
class RTNeuralWrapper {
public:
    RTNeuralWrapper();
    ~RTNeuralWrapper();

    /**
     * @brief Initialize wrapper with NAM model
     * @param model NAM model to load weights from (must be ready state)
     * @return true if structure initialized successfully (does not guarantee weight loading)
     * @pre model is non-null and in ready state
     * @post ready_ flag set; model can process (passthrough until weights loaded)
     * @note NOT RT-safe: allocates memory; call from loader/control thread only
     * @note Weight loading is currently TODO; process() returns passthrough
     */
    bool initialize(const NAMModel* model);

    /**
     * @brief Process single sample through neural network
     * @param input Input sample
     * @return Processed output sample (or passthrough if not ready/weights not loaded)
     * @pre initialize() was called successfully
     * @post Returns model output or input if not ready
     * @note RT-Safe: No allocations, locks, or I/O; callable from audio thread
     * @note Currently returns passthrough; will return model output after weight loading TODO
     */
    float process(float input);

    /**
     * @brief Reset neural network state
     * @post LSTM hidden/cell states cleared to zero
     * @note RT-Safe: Callable from audio thread
     */
    void reset();

    /**
     * @brief Check if model is ready for processing
     * @return true if initialize() succeeded and ready flag is set
     * @note RT-Safe: Uses atomic load with acquire semantics
     */
    bool isReady() const { return ready_.load(std::memory_order_acquire); }

    /**
     * @brief Get model architecture type
     * @return NAM architecture (LSTM, WaveNet, or Unknown)
     * @note RT-Safe: Returns cached value from initialization
     */
    NAMArchitecture getArchitecture() const { return architecture_; }

private:
    // PIMPL pattern: opaque pointer to RTNeural model implementation
    // Avoids including heavy RTNeural headers in this header file
    class ModelImpl;
    std::unique_ptr<ModelImpl> model_;

    // Configuration (cached from NAM model during initialization)
    NAMArchitecture architecture_{NAMArchitecture::kUnknown};
    int num_layers_{0};
    int hidden_size_{0};

    // Atomic ready flag for cross-thread communication
    // Written by initialize() (loader thread), read by process() (audio thread)
    std::atomic<bool> ready_{false};
};

} // namespace RenaAmp
