/*
 * Renamp — RTNeuralWrapper
 * Purpose: thin wrapper around RTNeural runtime model; provides process/reset.
 * Real-time safety: process() must be lock/alloc/I/O-free; initialization and logging occur off the audio thread.
 */
#include "dsp/rtneural_wrapper.h"
#include <iostream>

// RTNeural headers (only in .cpp to avoid heavy includes in header)
#include <RTNeural/RTNeural.h>

namespace RenaAmp {

// ============================================================================
// ModelImpl - PIMPL pattern implementation
// ============================================================================

class RTNeuralWrapper::ModelImpl {
public:
    ModelImpl() = default;
    ~ModelImpl() = default;

    // RTNeural model (runtime API)
    std::unique_ptr<RTNeural::Model<float>> model;

    // Process single sample
    float process(float input) {
        if (!model) {
            return input;
        }

        return model->forward(&input);
    }

    // Reset model state
    void reset() {
        if (model) {
            model->reset();
        }
    }
};

// ============================================================================
// RTNeuralWrapper Implementation
// ============================================================================

RTNeuralWrapper::RTNeuralWrapper()
    : model_(std::make_unique<ModelImpl>()) {
}

RTNeuralWrapper::~RTNeuralWrapper() = default;

bool RTNeuralWrapper::initialize(const NAMModel* model) {
    if (!model || !model->isReady()) {
        std::cerr << "RTNeuralWrapper: Invalid model" << std::endl;
        return false;
    }

    const NAMConfig& config = model->getConfig();
    architecture_ = config.architecture;
    num_layers_ = config.num_layers;
    hidden_size_ = config.hidden_size;

    // Initialize based on architecture
    if (config.architecture == NAMArchitecture::kLSTM) {
        std::cout << "RTNeuralWrapper: Initializing LSTM model"
                  << " (layers=" << num_layers_
                  << ", hidden=" << hidden_size_ << ")"
                  << std::endl;

        // TODO: Load actual weights from NAM model
        // For now, this is a placeholder that creates a simple model
        // In production, we would:
        // 1. Create RTNeural JSON configuration from NAM weights
        // 2. Parse JSON with RTNeural::json_parser
        // 3. Load model into model_->model

        std::cerr << "RTNeuralWrapper: LSTM weight loading not yet implemented" << std::endl;
        std::cerr << "Note: Using placeholder passthrough for now" << std::endl;

        ready_.store(true, std::memory_order_release);
        return true;

    } else if (config.architecture == NAMArchitecture::kWaveNet) {
        std::cerr << "RTNeuralWrapper: WaveNet not yet supported" << std::endl;
        std::cerr << "Note: Most NAM models (99.5%) use LSTM architecture" << std::endl;
        return false;
    }

    std::cerr << "RTNeuralWrapper: Unknown architecture" << std::endl;
    return false;
}

float RTNeuralWrapper::process(float input) {
    if (!ready_.load(std::memory_order_acquire)) {
        return input;  // Passthrough if not ready
    }

    // For now, passthrough until we implement weight loading
    return input;
}

void RTNeuralWrapper::reset() {
    if (model_) {
        model_->reset();
    }
}

} // namespace RenaAmp
