/*
 * Renamp — NAM to RTNeural Converter
 * Purpose: Convert NAM model weights/config to RTNeural runtime structures (off the audio thread).
 * Real-time safety: NOT RT-safe. Conversion and logging must never run in the audio callback.
 * Threading model: Control thread only (model loading path); no cross-thread communication.
 * Status: Partially implemented. Weight parsing works; RTNeural loading is TODO pending future integration.
 */
#pragma once

#include <memory>
#include <vector>
#include "models/nam_model.h"

// Forward declarations to avoid heavy RTNeural includes
namespace RTNeural {
    template<typename>
    class Model;
}

namespace RenaAmp {

/**
 * @brief NAM to RTNeural weight converter for LSTM models
 *
 * Converts NAM LSTM weights to RTNeural format to enable running NAM models
 * through RTNeural's inference engine as an alternative to NeuralAmpModelerCore.
 *
 * NAM LSTM format:
 * - Per layer: W[4*hidden, input+hidden], b[4*hidden], h0[hidden], c0[hidden]
 * - Head layer: weight[hidden], bias
 *
 * RTNeural LSTM format:
 * - W_in[input, 4*hidden]
 * - W_hidden[hidden, 4*hidden]
 * - bias[4*hidden]
 *
 * Note: This component is NOT RT-safe and must only run on the control thread.
 * Note: Partially implemented - weight parsing works; RTNeural model creation/loading is TODO.
 */
class NAMRTNeuralConverter {
public:
    /**
     * @brief Convert NAM LSTM weights to RTNeural format
     * @param nam_model Source NAM model (must be LSTM architecture, ready state)
     * @param rtneural_model Target RTNeural model to load weights into
     * @return true if weight parsing succeeded (does not guarantee RTNeural loading)
     * @pre nam_model is non-null, LSTM architecture, weights loaded
     * @post Weights parsed and validated; RTNeural model load is TODO (see implementation)
     * @note Non-RT: use only on control thread during model loading
     */
    static bool convertLSTMWeights(
        const NAMModel* nam_model,
        RTNeural::Model<float>& rtneural_model);

    /**
     * @brief Create RTNeural LSTM model from NAM config
     * @param config NAM model configuration (num_layers, hidden_size, input_size)
     * @return RTNeural model instance or nullptr if creation failed
     * @pre config is valid (num_layers > 0, hidden_size > 0)
     * @post Model structure created (weights must be loaded separately)
     * @note Currently returns nullptr; implementation is TODO pending RTNeural integration
     * @note Non-RT: use only on control thread during model loading
     */
    static std::unique_ptr<RTNeural::Model<float>> createLSTMModel(
        const NAMConfig& config);

private:
    // Weight parsing structures (intermediate format during conversion)
    struct LSTMLayerWeights {
        std::vector<std::vector<float>> W_in;      ///< Input weights [4*hidden][input]
        std::vector<std::vector<float>> W_hidden;  ///< Hidden weights [4*hidden][hidden]
        std::vector<float> bias;                   ///< Bias vector [4*hidden]
    };

    struct HeadWeights {
        std::vector<float> weight;  ///< Head weight vector [hidden]
        float bias;                 ///< Head bias scalar
    };
};

} // namespace RenaAmp
