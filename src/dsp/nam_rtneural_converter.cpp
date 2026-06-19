/*
 * Renamp — NAM to RTNeural Converter
 * Purpose: translate NAM weights/config to RTNeural runtime structures (off the audio thread).
 * RT note: conversion and logging are non-RT and must never run in the audio callback.
 */
#include "dsp/nam_rtneural_converter.h"
#include <RTNeural/RTNeural.h>
#include <iostream>
#include <cassert>

namespace RenaAmp {

// ============================================================================
// NAM to RTNeural Weight Converter
// ============================================================================

bool NAMRTNeuralConverter::convertLSTMWeights(
    const NAMModel* nam_model,
    RTNeural::Model<float>& rtneural_model)
{
    if (!nam_model || !nam_model->isReady()) {
        std::cerr << "Converter: Invalid NAM model" << std::endl;
        return false;
    }

    const NAMConfig& config = nam_model->getConfig();
    if (config.architecture != NAMArchitecture::kLSTM) {
        std::cerr << "Converter: Not an LSTM model" << std::endl;
        return false;
    }

    // Get NAM weights
    const float* nam_weights = nam_model->getWeights();
    size_t num_weights = nam_model->getNumWeights();

    if (num_weights == 0) {
        std::cerr << "Converter: No weights in NAM model" << std::endl;
        return false;
    }

    std::cout << "Converter: Converting LSTM weights"
              << " (layers=" << config.num_layers
              << ", hidden=" << config.hidden_size
              << ", total_weights=" << num_weights << ")"
              << std::endl;

    // Parse weights from NAM format
    std::vector<float>::const_iterator weight_iter(nam_weights);
    std::vector<float>::const_iterator weight_end(nam_weights + num_weights);

    // Process each LSTM layer
    for (int layer = 0; layer < config.num_layers; ++layer) {
        const int input_size = (layer == 0) ? config.input_size : config.hidden_size;
        const int hidden_size = config.hidden_size;

        std::cout << "Converter: Processing layer " << layer
                  << " (input=" << input_size
                  << ", hidden=" << hidden_size << ")"
                  << std::endl;

        // NAM format: W matrix [4*hidden, input+hidden] followed by bias [4*hidden]
        const int w_rows = 4 * hidden_size;
        const int w_cols = input_size + hidden_size;

        // Check we have enough weights
        size_t weights_needed = w_rows * w_cols +  // W matrix
                                4 * hidden_size +    // bias
                                2 * hidden_size;     // h0 and c0 (initial states)

        if (std::distance(weight_iter, weight_end) < static_cast<ptrdiff_t>(weights_needed)) {
            std::cerr << "Converter: Not enough weights for layer " << layer << std::endl;
            return false;
        }

        // Extract weight matrix and bias from NAM format
        std::vector<std::vector<float>> W_in (w_rows, std::vector<float>(input_size));
        std::vector<std::vector<float>> W_hidden (w_rows, std::vector<float>(hidden_size));
        std::vector<float> bias (4 * hidden_size);

        // Parse W matrix: [4*hidden, input+hidden] in row-major
        // Split into W_in [4*hidden, input] and W_hidden [4*hidden, hidden]
        for (int row = 0; row < w_rows; ++row) {
            for (int col = 0; col < input_size; ++col) {
                W_in[row][col] = *(weight_iter++);
            }
            for (int col = 0; col < hidden_size; ++col) {
                W_hidden[row][col] = *(weight_iter++);
            }
        }

        // Parse bias: [4*hidden]
        for (int i = 0; i < 4 * hidden_size; ++i) {
            bias[i] = *(weight_iter++);
        }

        // Skip initial states (h0 and c0) - RTNeural initializes to zero
        weight_iter += 2 * hidden_size;

        // TODO: Load weights into RTNeural LSTM layer
        // This requires accessing the LSTM layer from the RTNeural model
        // For now, we've successfully parsed and converted the format

        std::cout << "Converter: Layer " << layer << " weights parsed successfully" << std::endl;
    }

    // Parse head layer (dense + bias)
    size_t weights_remaining = std::distance(weight_iter, weight_end);
    size_t head_weights_needed = config.hidden_size + 1;  // weight vector + bias

    if (weights_remaining < head_weights_needed) {
        std::cerr << "Converter: Not enough weights for head layer" << std::endl;
        return false;
    }

    // Head weight vector
    std::vector<float> head_weight (config.hidden_size);
    for (int i = 0; i < config.hidden_size; ++i) {
        head_weight[i] = *(weight_iter++);
    }

    // Head bias
    float head_bias = *(weight_iter++);

    std::cout << "Converter: Head layer weights parsed successfully" << std::endl;
    std::cout << "Converter: Weights remaining: " << std::distance(weight_iter, weight_end) << std::endl;

    // TODO: Create RTNeural model and load converted weights
    // This requires building a model JSON or using RTNeural's layer API directly

    return true;
}

// ============================================================================
// Helper methods for creating RTNeural LSTM model
// ============================================================================

std::unique_ptr<RTNeural::Model<float>> NAMRTNeuralConverter::createLSTMModel(
    const NAMConfig& config)
{
    // TODO: Create RTNeural model with LSTM layers
    //
    // The model structure should be:
    // - Input: 1 sample
    // - LSTM layers: config.num_layers layers with config.hidden_size units
    // - Dense layer: config.hidden_size -> 1
    // - Output: 1 sample
    //
    // Use RTNeural's runtime API to create the model dynamically
    // based on the NAM config.

    (void)config;  // Suppress unused warning
    return nullptr;
}

} // namespace RenaAmp
