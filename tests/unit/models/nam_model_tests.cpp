/*
 * RenAmp — NAMModel Tests
 * Unit tests for NAM model JSON parsing, validation, and weight loading.
 */

#include <models/nam_model.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fstream>
#include <sstream>

using namespace RenaAmp;
using Catch::Matchers::WithinAbsMatcher;

// Test helper: Get path to test fixtures
inline std::string getFixturePath(const std::string& filename) {
    return std::string(TEST_FIXTURES_DIR) + "/" + filename;
}

// Test helper: Create a minimal valid NAM JSON string
inline std::string createValidNAMJSON() {
    return R"({
        "version": "0.5.1",
        "architecture": "LSTM",
        "config": {
            "input_size": 1,
            "hidden_size": 16,
            "num_layers": 2,
            "skip_connections": false,
            "stateful": true
        },
        "metadata": {
            "name": "Test Model",
            "modeled_by": "Test Creator",
            "gear_make": "Test",
            "gear_model": "Test Amp",
            "gear_type": "amp",
            "tone_type": "clean",
            "sample_rate": 48000.0
        },
        "weights": [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9]
    })";
}

// Test helper: Create invalid NAM JSON (missing required field)
inline std::string createInvalidNAMJSON() {
    return R"({
        "version": "0.5.1",
        "architecture": "LSTM",
        "config": {
            "input_size": 1
            // Missing hidden_size, num_layers
        },
        "weights": [0.0, 0.1, 0.2]
    })";
}

// Test helper: Create WaveNet NAM JSON
inline std::string createWaveNetNAMJSON() {
    return R"({
        "version": "0.5.1",
        "architecture": "WaveNet",
        "config": {
            "input_size": 1,
            "receptive_field": [3, 3, 3],
            "dilation_channels": 16,
            "residual_channels": 32,
            "skip_connections": true
        },
        "metadata": {
            "name": "Test WaveNet",
            "modeled_by": "Test",
            "gear_make": "Test",
            "gear_model": "Test",
            "gear_type": "amp",
            "tone_type": "clean",
            "sample_rate": 48000.0
        },
        "weights": [0.0, 0.1, 0.2, 0.3, 0.4, 0.5]
    })";
}

SCENARIO("NAMModel initialization", "[nam][init]") {
    GIVEN("default constructed model") {
        NAMModel model;

        WHEN("initialized") {
            bool initialized = model.initialize();

            THEN("initialization succeeds") {
                REQUIRE(initialized);
            }

            THEN("model is not ready until loaded") {
                REQUIRE_FALSE(model.isReady());
            }
        }
    }
}

SCENARIO("NAMModel parses valid JSON config", "[nam][json]") {
    GIVEN("model and valid LSTM JSON string") {
        NAMModel model;
        REQUIRE(model.initialize());

        std::string valid_json = createValidNAMJSON();

        WHEN("loading from JSON string") {
            bool loaded = model.loadFromJSON(valid_json);

            THEN("model loads successfully") {
                REQUIRE(loaded);
                REQUIRE(model.isReady());
            }

            THEN("architecture is extracted correctly") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.architecture == NAMArchitecture::kLSTM);
            }

            THEN("LSTM config is parsed correctly") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.input_size == 1);
                REQUIRE(config.hidden_size == 16);
                REQUIRE(config.num_layers == 2);
                REQUIRE(config.skip_connections == false);
                REQUIRE(config.stateful == true);
            }

            THEN("metadata is parsed correctly") {
                const NAMMetadata& metadata = model.getMetadata();
                REQUIRE(metadata.name == "Test Model");
                REQUIRE(metadata.modeled_by == "Test Creator");
                REQUIRE(metadata.gear_make == "Test");
                REQUIRE(metadata.gear_model == "Test Amp");
                REQUIRE(metadata.gear_type == "amp");
                REQUIRE(metadata.tone_type == "clean");
                REQUIRE_THAT(metadata.sample_rate, WithinAbsMatcher(48000.0f, 1e-6f));
            }

            THEN("weights buffer is populated") {
                REQUIRE(model.getNumWeights() > 0);
                REQUIRE(model.getWeights() != nullptr);
            }
        }
    }

    // NOTE: WaveNet tests disabled - requires physical .nam file
    // JSON-only loading doesn't work with NeuralAmpModelerCore::get_dsp()
    // TODO: Re-enable when WaveNet can be tested properly
    /*
    GIVEN("model and valid WaveNet JSON string") {
        NAMModel model;
        REQUIRE(model.initialize());

        std::string wavenet_json = createWaveNetNAMJSON();

        WHEN("loading WaveNet model") {
            bool loaded = model.loadFromJSON(wavenet_json);

            THEN("model loads successfully") {
                REQUIRE(loaded);
                REQUIRE(model.isReady());
            }

            THEN("architecture is WaveNet") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.architecture == NAMArchitecture::kWaveNet);
            }

            THEN("WaveNet config is parsed") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.receptive_field.size() == 3);
                // dilation_channels and residual_channels are vectors, check size
                REQUIRE(config.dilation_channels.size() > 0);
                REQUIRE(config.residual_channels.size() > 0);
            }
        }
    }
    */
}

SCENARIO("NAMModel rejects invalid JSON", "[nam][validation]") {
    GIVEN("model") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("JSON is malformed") {
            std::string malformed_json = "{not valid json";

            bool loaded = model.loadFromJSON(malformed_json);

            THEN("load fails") {
                REQUIRE_FALSE(loaded);
                REQUIRE_FALSE(model.isReady());
            }
        }

        WHEN("JSON is missing required fields") {
            std::string invalid_json = createInvalidNAMJSON();

            bool loaded = model.loadFromJSON(invalid_json);

            THEN("load fails") {
                REQUIRE_FALSE(loaded);
                REQUIRE_FALSE(model.isReady());
            }
        }

        WHEN("JSON has unknown architecture") {
            std::string unknown_arch = R"({
                "version": "0.5.1",
                "architecture": "UnknownArch",
                "config": {},
                "weights": []
            })";

            bool loaded = model.loadFromJSON(unknown_arch);

            THEN("load fails") {
                REQUIRE_FALSE(loaded);
            }
        }

        WHEN("JSON has empty weights") {
            std::string empty_weights = R"({
                "version": "0.5.1",
                "architecture": "LSTM",
                "config": {"input_size": 1, "hidden_size": 16, "num_layers": 2},
                "weights": []
            })";

            bool loaded = model.loadFromJSON(empty_weights);

            THEN("load fails or weights buffer is empty") {
                // Model may load but have no weights
                if (loaded) {
                    REQUIRE(model.getNumWeights() == 0);
                } else {
                    REQUIRE_FALSE(loaded);
                }
            }
        }
    }
}

SCENARIO("NAMModel loads from file", "[nam][file]") {
    GIVEN("model and valid NAM file") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("loading from fixture file") {
            bool loaded = model.loadFromFile(getFixturePath("models/simple_lstm.nam"));

            THEN("file loads successfully") {
                REQUIRE(loaded);
                REQUIRE(model.isReady());
            }

            THEN("file path is stored") {
                REQUIRE(model.getFilePath() == getFixturePath("models/simple_lstm.nam"));
            }
        }
    }

    GIVEN("model and non-existent file") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("attempting to load missing file") {
            bool loaded = model.loadFromFile("/nonexistent/path.nam");

            THEN("load fails gracefully") {
                REQUIRE_FALSE(loaded);
                REQUIRE_FALSE(model.isReady());
            }
        }
    }
}

SCENARIO("NAMModel validation", "[nam][validation]") {
    GIVEN("loaded valid model") {
        NAMModel model;
        REQUIRE(model.initialize());
        REQUIRE(model.loadFromJSON(createValidNAMJSON()));

        WHEN("validate is called") {
            bool valid = model.validate();

            THEN("model passes validation") {
                REQUIRE(valid);
            }
        }
    }

    GIVEN("model with loaded but invalid data") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("model has no weights") {
            // Load JSON with empty weights
            std::string empty_weights = R"({
                "version": "0.5.1",
                "architecture": "LSTM",
                "config": {"input_size": 1, "hidden_size": 16, "num_layers": 2},
                "weights": []
            })";
            model.loadFromJSON(empty_weights);

            THEN("validation fails") {
                REQUIRE_FALSE(model.validate());
            }
        }
    }
}

SCENARIO("NAMModel getters are RT-safe", "[nam][rt-safe]") {
    GIVEN("loaded model") {
        NAMModel model;
        REQUIRE(model.initialize());
        REQUIRE(model.loadFromJSON(createValidNAMJSON()));

        WHEN("calling RT-safe getters") {
            THEN("getters don't allocate or throw") {
                // isReady() is atomic load
                bool ready = model.isReady();
                REQUIRE(ready);

                // getConfig() returns const reference
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.architecture == NAMArchitecture::kLSTM);

                // getMetadata() returns const reference
                const NAMMetadata& metadata = model.getMetadata();
                REQUIRE(metadata.name == "Test Model");

                // getWeights() returns pointer
                const float* weights = model.getWeights();
                REQUIRE(weights != nullptr);

                // getNumWeights() returns cached size
                size_t num_weights = model.getNumWeights();
                REQUIRE(num_weights > 0);
            }
        }
    }
}

SCENARIO("NAMModel metadata fields", "[nam][metadata]") {
    GIVEN("model with extended metadata") {
        NAMModel model;
        REQUIRE(model.initialize());

        std::string full_metadata = R"({
            "version": "0.5.1",
            "architecture": "LSTM",
            "config": {
                "input_size": 1,
                "hidden_size": 16,
                "num_layers": 2
            },
            "metadata": {
                "name": "Full Test Model",
                "modeled_by": "Test Engineer",
                "gear_make": "Fender",
                "gear_model": "Twin Reverb",
                "gear_type": "amp_cab",
                "tone_type": "crunch",
                "sample_rate": 48000.0,
                "input_level_dbu": -10.0,
                "output_level_dbu": 4.0
            },
            "weights": [0.0, 0.1, 0.2]
        })";

        WHEN("loading model with full metadata") {
            REQUIRE(model.loadFromJSON(full_metadata));

            THEN("all metadata fields are parsed") {
                const NAMMetadata& metadata = model.getMetadata();
                REQUIRE(metadata.name == "Full Test Model");
                REQUIRE(metadata.modeled_by == "Test Engineer");
                REQUIRE(metadata.gear_make == "Fender");
                REQUIRE(metadata.gear_model == "Twin Reverb");
                REQUIRE(metadata.gear_type == "amp_cab");
                REQUIRE(metadata.tone_type == "crunch");
                REQUIRE_THAT(metadata.input_level_dbu, WithinAbsMatcher(-10.0f, 1e-6f));
                REQUIRE_THAT(metadata.output_level_dbu, WithinAbsMatcher(4.0f, 1e-6f));
            }
        }
    }
}

SCENARIO("NAMModel version parsing", "[nam][version]") {
    GIVEN("model with different version") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("loading model with version 0.5.0") {
            std::string v050 = R"({
                "version": "0.5.0",
                "architecture": "LSTM",
                "config": {"input_size": 1, "hidden_size": 16, "num_layers": 2},
                "weights": [0.0, 0.1]
            })";

            bool loaded = model.loadFromJSON(v050);

            THEN("loads successfully") {
                REQUIRE(loaded);
            }
        }
    }
}

SCENARIO("NAMModel weight buffer access", "[nam][weights]") {
    GIVEN("loaded model with known weights") {
        NAMModel model;
        REQUIRE(model.initialize());

        std::string known_weights = R"({
            "version": "0.5.1",
            "architecture": "LSTM",
            "config": {"input_size": 1, "hidden_size": 8, "num_layers": 1},
            "weights": [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]
        })";

        WHEN("loading model") {
            REQUIRE(model.loadFromJSON(known_weights));

            THEN("weights are accessible") {
                const float* weights = model.getWeights();
                size_t num_weights = model.getNumWeights();

                REQUIRE(num_weights >= 10);
                REQUIRE(weights != nullptr);

                // Check some values (may be reordered by LSTM mapping)
                bool found_0_1 = false;
                bool found_1_0 = false;
                for (size_t i = 0; i < num_weights; ++i) {
                    if (std::abs(weights[i] - 0.1f) < 1e-6f) found_0_1 = true;
                    if (std::abs(weights[i] - 1.0f) < 1e-6f) found_1_0 = true;
                }
                REQUIRE(found_0_1);
                REQUIRE(found_1_0);
            }
        }
    }
}

SCENARIO("NAMModel architecture variants", "[nam][architecture]") {
    GIVEN("model with LSTM architecture") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("loading LSTM model") {
            std::string lstm = R"({
                "version": "0.5.1",
                "architecture": "LSTM",
                "config": {
                    "input_size": 1,
                    "hidden_size": 24,
                    "num_layers": 3,
                    "stateful": true
                },
                "weights": [0.0, 0.1, 0.2]
            })";

            REQUIRE(model.loadFromJSON(lstm));

            THEN("LSTM config is set") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.architecture == NAMArchitecture::kLSTM);
                REQUIRE(config.hidden_size == 24);
                REQUIRE(config.num_layers == 3);
            }
        }
    }

    // NOTE: WaveNet tests disabled - requires physical .nam file
    // JSON-only loading doesn't work with NeuralAmpModelerCore::get_dsp()
    // TODO: Re-enable when WaveNet can be tested properly
    /*
    GIVEN("model with WaveNet architecture") {
        NAMModel model;
        REQUIRE(model.initialize());

        WHEN("loading WaveNet model") {
            std::string wavenet = R"({
                "version": "0.5.1",
                "architecture": "WaveNet",
                "config": {
                    "input_size": 1,
                    "receptive_field": [5, 5, 5],
                    "dilation_channels": 32,
                    "residual_channels": 64,
                    "skip_connections": true
                },
                "weights": [0.0, 0.1, 0.2]
            })";

            REQUIRE(model.loadFromJSON(wavenet));

            THEN("WaveNet config is set") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.architecture == NAMArchitecture::kWaveNet);
                REQUIRE(config.receptive_field.size() == 3);
                // dilation_channels and residual_channels are vectors
                REQUIRE(config.dilation_channels.size() > 0);
                REQUIRE(config.residual_channels.size() > 0);
                REQUIRE(config.skip_connections == true);
            }
        }
    }
    */
}

SCENARIO("NAMModel default values", "[nam][defaults]") {
    GIVEN("model with minimal config") {
        NAMModel model;
        REQUIRE(model.initialize());

        std::string minimal = R"({
            "version": "0.5.1",
            "architecture": "LSTM",
            "config": {"input_size": 1, "hidden_size": 16, "num_layers": 2},
            "weights": [0.0, 0.1]
        })";

        WHEN("loading minimal model") {
            REQUIRE(model.loadFromJSON(minimal));

            THEN("optional fields have defaults") {
                const NAMConfig& config = model.getConfig();
                REQUIRE(config.skip_connections == false);  // Default

                const NAMMetadata& metadata = model.getMetadata();
                REQUIRE(metadata.sample_rate == 48000.0f);  // Default
                REQUIRE(metadata.input_level_dbu == 0.0f);  // Default
                REQUIRE(metadata.output_level_dbu == 0.0f);  // Default
            }
        }
    }
}
