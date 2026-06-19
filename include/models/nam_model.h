/*
 * Renamp — NAM Model
 * Purpose: Load, parse, validate, and store Neural Amp Modeler (NAM) model weights and configuration.
 * Real-time safety: isReady(), getWeights(), getNumWeights() are RT-safe (no allocations).
 *                  loadFromFile/loadFromJSON are NOT RT-safe (file I/O, parsing, allocations).
 * Threading model: Loader thread loads models; audio thread reads weights/config via const getters.
 *                 Cross-thread communication via atomic ready flag.
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <array>

namespace RenaAmp {

/**
 * @brief NAM model architecture types
 */
enum class NAMArchitecture {
    kLSTM,           ///< Recurrent LSTM architecture (most common, ~99% of models)
    kWaveNet,        ///< Convolutional WaveNet architecture
    kUnknown         ///< Unsupported/unknown architecture
};

/**
 * @brief NAM model configuration
 * Parsed from the "config" section of .nam file
 */
struct NAMConfig {
    NAMArchitecture architecture;
    int num_layers;              ///< Number of layers (LSTM) or receptive field (WaveNet)
    int input_size;              ///< Input dimension (typically 1 for mono audio)
    int hidden_size;             ///< Hidden layer size
    bool skip_connections;        ///< Whether model uses skip connections
    float bias;                  ///< Model bias term

    // LSTM-specific
    bool stateful;                ///< Whether LSTM maintains state between calls

    // WaveNet-specific
    std::vector<int> receptive_field;     ///< Receptive field per layer
    std::vector<int> dilation_channels;   ///< Dilation channels per layer
    std::vector<int> residual_channels;   ///< Residual channels per layer

    // Default constructor
    NAMConfig()
        : architecture(NAMArchitecture::kUnknown)
        , num_layers(0)
        , input_size(1)
        , hidden_size(0)
        , skip_connections(false)
        , bias(0.0f)
        , stateful(false)
    {}
};

/**
 * @brief NAM model metadata
 * Parsed from the "metadata" section of .nam file
 */
struct NAMMetadata {
    std::string version;          ///< NAM format version
    std::string name;             ///< Model display name
    std::string modeled_by;       ///< Creator/trainer
    std::string gear_make;        ///< Gear manufacturer (e.g., "Fender")
    std::string gear_model;       ///< Gear model (e.g., "Twin Reverb")
    std::string gear_type;        ///< "amp", "pedal", "amp_cab", etc.
    std::string tone_type;        ///< "clean", "crunch", "hi_gain", etc.
    float sample_rate;            ///< Training sample rate (for compatibility check)
    float input_level_dbu;        ///< Reference input level
    float output_level_dbu;       ///< Reference output level

    // Training metadata (optional)
    bool has_training_data;        ///< Whether training metadata is available
    int epoch;                    ///< Training epoch
    float loss;                   ///< Training loss

    // Default constructor
    NAMMetadata()
        : sample_rate(48000.0f)
        , input_level_dbu(0.0f)
        , output_level_dbu(0.0f)
        , has_training_data(false)
        , epoch(0)
        , loss(0.0f)
    {}
};

/**
 * @brief NAM model container for Neural Amp Modeler
 *
 * Handles loading, validation, and storage of NAM model weights and configuration.
 * Provides RT-safe weight access for audio thread inference.
 *
 * Responsibilities:
 * - Parse .nam JSON files (version, architecture, config, weights, metadata)
 * - Validate model format and weight dimensions
 * - Store weights in pre-allocated buffer
 * - Provide RT-safe const access to weights and configuration
 *
 * Note: This class does NOT perform inference. Use NAMProcessor or RTNeuralWrapper for that.
 */
class NAMModel {
public:
    NAMModel();
    ~NAMModel();

    /**
     * @brief Initialize model system and pre-allocate memory
     * @return true if initialization succeeded
     * @post Memory reserved for model weights (max ~10MB)
     * @note NOT RT-safe: call during setup/loader thread
     */
    bool initialize();

    /**
     * @brief Load model from .nam file
     * @param filepath Path to .nam file
     * @return true if loaded and validated successfully
     * @post ready_ flag set; weights buffer populated; config/metadata parsed
     * @note NOT RT-safe: performs file I/O and allocation; call from loader thread only
     */
    bool loadFromFile(const std::string& filepath);

    /**
     * @brief Load model from JSON string (for testing)
     * @param json_str JSON string containing model data
     * @return true if loaded and validated successfully
     * @note NOT RT-safe: parses JSON and allocates; for testing/loader thread only
     */
    bool loadFromJSON(const std::string& json);

    /**
     * @brief Validate model format and weights
     * @return true if model passes validation checks
     * @note Checks: architecture known, weights non-empty, dimensions valid
     */
    bool validate() const;

    /**
     * @brief Get model configuration
     * @return Const reference to NAMConfig
     * @note RT-Safe: returns cached struct
     */
    const NAMConfig& getConfig() const { return config_; }

    /**
     * @brief Get model metadata
     * @return Const reference to NAMMetadata
     * @note RT-Safe: returns cached struct
     */
    const NAMMetadata& getMetadata() const { return metadata_; }

    /**
     * @brief Check if model is loaded and ready for use
     * @return true if model is in ready state
     * @note RT-Safe: atomic load with acquire semantics
     */
    bool isReady() const { return ready_.load(std::memory_order_acquire); }

    /**
     * @brief Get weight buffer pointer
     * @return Pointer to pre-allocated weight buffer (or nullptr if empty)
     * @note RT-Safe: returns pointer to static buffer; no allocation
     * @note Caller must ensure isReady() returns true before accessing
     */
    const float* getWeights() const { return weights_buffer_.data(); }

    /**
     * @brief Get number of weights in buffer
     * @return Number of float weights
     * @note RT-Safe: returns cached size
     */
    size_t getNumWeights() const { return weights_buffer_.size(); }

    /**
     * @brief Get model file path
     * @return Path to .nam file used for loading
     * @note RT-Safe: returns cached string
     */
    const std::string& getFilePath() const { return file_path_; }

private:
    // Model data (parsed from .nam file)
    NAMConfig config_;                         ///< Model architecture configuration
    NAMMetadata metadata_;                     ///< Model metadata
    std::vector<float> weights_buffer_;        ///< Pre-allocated weight storage

    // File info
    std::string file_path_;                    ///< Path to source .nam file

    // Atomic ready flag for cross-thread communication
    // Written by load methods (loader thread), read by isReady() (audio thread)
    std::atomic<bool> ready_{false};

    // JSON parsing helpers (called internally during load)
    bool parseVersion(const std::string& json);         ///< Parse and validate NAM version
    bool parseArchitecture(const std::string& json);    ///< Determine architecture type
    bool parseConfig(const std::string& json);          ///< Parse model config section
    bool parseWeights(const std::string& json);         ///< Extract weight array
    bool parseMetadata(const std::string& json);        ///< Parse metadata section

    // Architecture-specific weight mapping
    bool mapLSTMWeights(const std::vector<float>& raw_weights);   ///< Re-map LSTM weights to internal format
    bool mapWaveNetWeights(const std::vector<float>& raw_weights); ///< Re-map WaveNet weights to internal format
};

/**
 * @brief NAM model manager for preset switching
 *
 * Manages up to 4 NAM models in memory and provides hot-swapping functionality.
 * Use for preset banks where rapid model switching is required.
 *
 * Threading: Loader thread calls loadModel(); control thread calls selectModel();
 *             audio thread calls getActiveModel().
 */
class NAMModelManager {
public:
    static constexpr size_t MAX_CACHED_MODELS = 4;

    NAMModelManager();
    ~NAMModelManager() = default;

    /**
     * @brief Initialize model manager
     * @post All model slots initialized and ready for loading
     * @note Call during setup before loading any models
     */
    void initialize();

    /**
     * @brief Load model into cache
     * @param filepath Path to .nam file
     * @return true if loaded successfully (false if cache full or load failed)
     * @note NOT RT-safe: performs file I/O; call from loader thread only
     */
    bool loadModel(const std::string& filepath);

    /**
     * @brief Select active model by index
     * @param index Model index (0 to num_loaded-1)
     * @return true if model selected (false if index invalid)
     * @note Thread-safe: can be called from control thread
     */
    bool selectModel(size_t index);

    /**
     * @brief Get active model for inference
     * @return Pointer to active NAMModel (or nullptr if none selected)
     * @note RT-Safe: returns const pointer; no allocation
     * @note Caller must check returned pointer is non-null before use
     */
    const NAMModel* getActiveModel() const;

    /**
     * @brief Get number of cached models
     * @return Number of successfully loaded models (0 to MAX_CACHED_MODELS)
     * @note RT-Safe: atomic load
     */
    size_t getNumModels() const;

private:
    std::array<std::unique_ptr<NAMModel>, MAX_CACHED_MODELS> models_;  ///< Model cache slots
    std::atomic<size_t> active_model_index_{0};                        ///< Current active model index
    std::atomic<size_t> num_loaded_{0};                                 ///< Number of loaded models
};

} // namespace RenaAmp
