/*
 * Renamp — NAMModel / NAMModelManager
 * Purpose: parse and validate NAM model JSON and weights; manage active model.
 * RT note: all I/O and parsing run off the audio thread; audio thread uses const pointers/atomics only.
 */
#include "models/nam_model.h"

// Define JSON_NOEXCEPTION before including nlohmann/json
// This is required because we compile with -fno-exceptions for RT-safety
#define JSON_NOEXCEPTION
#include <nlohmann/json.hpp>
#undef JSON_NOEXCEPTION

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace RenaAmp {

// ============================================================================
// NAMModel Implementation
// ============================================================================

NAMModel::NAMModel() = default;

NAMModel::~NAMModel() = default;

bool NAMModel::initialize() {
    // Pre-allocate weight buffer (max 10MB for typical models)
    weights_buffer_.reserve(10 * 1024 * 1024 / sizeof(float));  // ~2.6M floats
    return true;
}

bool NAMModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open NAM file: " << filepath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();

    file_path_ = filepath;
    return loadFromJSON(json_str);
}

bool NAMModel::loadFromJSON(const std::string& json_str) {
    // Parse JSON (no exceptions mode)
    if (!json::accept(json_str)) {
        std::cerr << "Invalid JSON format" << std::endl;
        return false;
    }

    // Parse version
    if (!parseVersion(json_str)) {
        std::cerr << "Unsupported NAM version" << std::endl;
        return false;
    }

    // Parse architecture
    if (!parseArchitecture(json_str)) {
        std::cerr << "Unknown or unsupported architecture" << std::endl;
        return false;
    }

    // Parse configuration
    if (!parseConfig(json_str)) {
        std::cerr << "Failed to parse model config" << std::endl;
        return false;
    }

    // Parse weights
    if (!parseWeights(json_str)) {
        std::cerr << "Failed to parse model weights" << std::endl;
        return false;
    }

    // Parse metadata (optional)
    parseMetadata(json_str);

    // Validate model
    if (!validate()) {
        std::cerr << "Model validation failed" << std::endl;
        return false;
    }

    ready_.store(true, std::memory_order_release);
    return true;
}

bool NAMModel::validate() const {
    if (config_.architecture == NAMArchitecture::kUnknown) {
        std::cerr << "Unknown architecture" << std::endl;
        return false;
    }

    if (weights_buffer_.empty()) {
        std::cerr << "No weights loaded" << std::endl;
        return false;
    }

    // Architecture-specific validation
    if (config_.architecture == NAMArchitecture::kLSTM) {
        if (config_.hidden_size <= 0) {
            std::cerr << "Invalid hidden size for LSTM" << std::endl;
            return false;
        }
        if (config_.num_layers <= 0) {
            std::cerr << "Invalid num_layers for LSTM" << std::endl;
            return false;
        }
    }
    // WaveNet doesn't have hidden_size/num_layers, uses channels instead
    else if (config_.architecture == NAMArchitecture::kWaveNet) {
        // WaveNet validation is more lenient
        // The model will be validated by NeuralAmpModelerCore
        std::cout << "WaveNet model loaded, validation deferred to NeuralAmpModelerCore" << std::endl;
    }

    // Sample rate compatibility check
    if (metadata_.sample_rate > 0 && std::abs(metadata_.sample_rate - 48000.0f) > 1000.0f) {
        std::cout << "Warning: Model trained at " << metadata_.sample_rate
                  << " Hz, running at 48000 Hz" << std::endl;
    }

    return true;
}

bool NAMModel::parseVersion(const std::string& json_str) {
    json j = json::parse(json_str);

    if (j.contains("version")) {
        metadata_.version = j["version"].get<std::string>();
        if (metadata_.version[0] != '0' && metadata_.version[0] != '1') {
            return false;
        }
    }

    return true;
}

bool NAMModel::parseArchitecture(const std::string& json_str) {
    json j = json::parse(json_str);

    if (j.contains("architecture")) {
        std::string arch_str = j["architecture"].get<std::string>();

        if (arch_str == "LSTM") {
            config_.architecture = NAMArchitecture::kLSTM;
            return true;
        } else if (arch_str == "WaveNet") {
            config_.architecture = NAMArchitecture::kWaveNet;
            return true;
        } else if (arch_str == "CatLSTM" || arch_str == "ConvNet") {
            config_.architecture = NAMArchitecture::kUnknown;
            return false;
        }
    }

    // Default to LSTM
    config_.architecture = NAMArchitecture::kLSTM;
    return true;
}

bool NAMModel::parseConfig(const std::string& json_str) {
    json j = json::parse(json_str);

    if (!j.contains("config")) {
        // Use defaults
        config_.num_layers = 2;
        config_.input_size = 1;
        config_.hidden_size = 128;
        config_.skip_connections = false;
        config_.bias = 0.0f;
        return true;
    }

    json config = j["config"];

    if (config.contains("num_layers")) {
        config_.num_layers = config["num_layers"].get<int>();
    }

    if (config.contains("input_size")) {
        config_.input_size = config["input_size"].get<int>();
    }

    if (config.contains("hidden_size")) {
        config_.hidden_size = config["hidden_size"].get<int>();
    }

    if (config.contains("skip_connections")) {
        config_.skip_connections = config["skip_connections"].get<bool>();
    }

    if (config.contains("bias")) {
        config_.bias = config["bias"].get<float>();
    }

    if (config_.architecture == NAMArchitecture::kLSTM) {
        if (config.contains("stateful")) {
            config_.stateful = config["stateful"].get<bool>();
        }
    }

    if (config_.architecture == NAMArchitecture::kWaveNet) {
        if (config.contains("receptive_field")) {
            config_.receptive_field = config["receptive_field"].get<std::vector<int>>();
        }
        if (config.contains("dilation_channels")) {
            config_.dilation_channels = config["dilation_channels"].get<std::vector<int>>();
        }
        if (config.contains("residual_channels")) {
            config_.residual_channels = config["residual_channels"].get<std::vector<int>>();
        }
    }

    return true;
}

bool NAMModel::parseWeights(const std::string& json_str) {
    json j = json::parse(json_str);

    if (!j.contains("weights")) {
        std::cerr << "No weights in model file" << std::endl;
        return false;
    }

    json weights_json = j["weights"];
    std::vector<float> raw_weights;

    if (weights_json.is_array()) {
        raw_weights = weights_json.get<std::vector<float>>();
    } else if (weights_json.is_string()) {
        std::cerr << "Base64 encoded weights not yet implemented" << std::endl;
        return false;
    } else {
        std::cerr << "Unknown weight format" << std::endl;
        return false;
    }

    // Map weights based on architecture
    if (config_.architecture == NAMArchitecture::kLSTM) {
        return mapLSTMWeights(raw_weights);
    } else if (config_.architecture == NAMArchitecture::kWaveNet) {
        return mapWaveNetWeights(raw_weights);
    }

    return false;
}

bool NAMModel::mapLSTMWeights(const std::vector<float>& raw_weights) {
    weights_buffer_ = raw_weights;
    std::cout << "Loaded LSTM model: " << raw_weights.size() << " weights" << std::endl;
    return !weights_buffer_.empty();
}

bool NAMModel::mapWaveNetWeights(const std::vector<float>& raw_weights) {
    weights_buffer_ = raw_weights;
    std::cout << "Loaded WaveNet model: " << raw_weights.size() << " weights" << std::endl;
    return !weights_buffer_.empty();
}

bool NAMModel::parseMetadata(const std::string& json_str) {
    json j = json::parse(json_str);

    if (!j.contains("metadata")) {
        return true;
    }

    json meta = j["metadata"];

    if (meta.contains("name")) {
        metadata_.name = meta["name"].get<std::string>();
    }

    if (meta.contains("modeled_by")) {
        metadata_.modeled_by = meta["modeled_by"].get<std::string>();
    }

    if (meta.contains("gear_make")) {
        metadata_.gear_make = meta["gear_make"].get<std::string>();
    }

    if (meta.contains("gear_model")) {
        metadata_.gear_model = meta["gear_model"].get<std::string>();
    }

    if (meta.contains("gear_type")) {
        metadata_.gear_type = meta["gear_type"].get<std::string>();
    }

    if (meta.contains("tone_type")) {
        metadata_.tone_type = meta["tone_type"].get<std::string>();
    }

    if (meta.contains("sample_rate")) {
        metadata_.sample_rate = meta["sample_rate"].get<float>();
    }

    if (meta.contains("input_level_dbu")) {
        metadata_.input_level_dbu = meta["input_level_dbu"].get<float>();
    }

    if (meta.contains("output_level_dbu")) {
        metadata_.output_level_dbu = meta["output_level_dbu"].get<float>();
    }

    if (meta.contains("epoch")) {
        metadata_.has_training_data = true;
        metadata_.epoch = meta["epoch"].get<int>();
    }

    if (meta.contains("loss")) {
        metadata_.loss = meta["loss"].get<float>();
    }

    std::cout << "Model: " << metadata_.name
              << " (" << metadata_.gear_make << " " << metadata_.gear_model << ")"
              << std::endl;

    return true;
}

// ============================================================================
// NAMModelManager Implementation
// ============================================================================

NAMModelManager::NAMModelManager() {
    for (size_t i = 0; i < MAX_CACHED_MODELS; ++i) {
        models_[i] = std::make_unique<NAMModel>();
    }
}

void NAMModelManager::initialize() {
    for (size_t i = 0; i < MAX_CACHED_MODELS; ++i) {
        models_[i]->initialize();
    }
}

bool NAMModelManager::loadModel(const std::string& filepath) {
    size_t index = num_loaded_.load();

    if (index >= MAX_CACHED_MODELS) {
        std::cerr << "Model cache full, max " << MAX_CACHED_MODELS << " models" << std::endl;
        return false;
    }

    if (models_[index]->loadFromFile(filepath)) {
        num_loaded_.fetch_add(1);
        return true;
    }

    return false;
}

bool NAMModelManager::selectModel(size_t index) {
    if (index >= num_loaded_.load()) {
        std::cerr << "Invalid model index: " << index << std::endl;
        return false;
    }

    if (!models_[index]->isReady()) {
        std::cerr << "Model not ready: " << index << std::endl;
        return false;
    }

    active_model_index_.store(index);
    return true;
}

const NAMModel* NAMModelManager::getActiveModel() const {
    size_t index = active_model_index_.load();

    if (index >= num_loaded_.load()) {
        return nullptr;
    }

    return models_[index].get();
}

size_t NAMModelManager::getNumModels() const {
    return num_loaded_.load();
}

} // namespace RenaAmp
