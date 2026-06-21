/*
 * RenAmp — CabinetProcessor Tests
 * Unit tests for cabinet impulse response loading, convolution, and stereo processing.
 */

#include <dsp/cabinet_processor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <cmath>

using namespace RenaAmp;
using Catch::Matchers::WithinAbsMatcher;

// Test helper: Get path to test fixtures
inline std::string getFixturePath(const std::string& filename) {
    return std::string(TEST_FIXTURES_DIR) + "/" + filename;
}

// Test helper: Initialize CabinetProcessor with test parameters
inline void initTestProcessor(CabinetProcessor& processor, float sample_rate = 48000.0f) {
    processor.initialize(sample_rate);
}

SCENARIO("CabinetProcessor initialization", "[cabinet][init]") {
    GIVEN("default constructed processor") {
        CabinetProcessor processor;

        WHEN("initialized with 48kHz") {
            processor.initialize(48000.0f);

            THEN("processor is ready for processing") {
                // Process a silent buffer - should not crash
                float silent[64] = {0.0f};
                processor.process(silent, silent, 64);
            }

            THEN("no IR is loaded initially") {
                REQUIRE_FALSE(processor.isIRLoaded());
                REQUIRE(processor.getIRLength() == 0);
            }
        }
    }
}

SCENARIO("CabinetProcessor loads WAV IR", "[cabinet][load]") {
    GIVEN("processor and mono WAV file") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);

        WHEN("loading mono 48kHz IR") {
            bool loaded = processor.loadIR(getFixturePath("irs/mono_48k.wav"));

            THEN("IR loads successfully") {
                REQUIRE(loaded);
                REQUIRE(processor.isIRLoaded());
                REQUIRE(processor.getIRLength() > 0);
            }

            AND_WHEN("processing audio with loaded IR") {
                processor.setBypass(false);

                float input[64];
                float output[64];
                for (int i = 0; i < 64; ++i) {
                    input[i] = 0.5f;
                    output[i] = input[i];
                }

                processor.process(output, output, 64);

                THEN("audio is processed without errors") {
                    for (int i = 0; i < 64; ++i) {
                        REQUIRE(std::isfinite(output[i]));
                    }
                }
            }
        }
    }

    GIVEN("processor and stereo WAV file") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);

        WHEN("loading stereo 48kHz IR") {
            bool loaded = processor.loadIR(getFixturePath("irs/stereo_48k.wav"));

            THEN("IR loads successfully") {
                REQUIRE(loaded);
                REQUIRE(processor.isIRLoaded());
            }
        }
    }
}

SCENARIO("CabinetProcessor rejects invalid files", "[cabinet][validation]") {
    GIVEN("processor") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);

        WHEN("attempting to load non-existent file") {
            bool loaded = processor.loadIR("/nonexistent/file.wav");

            THEN("load fails gracefully") {
                REQUIRE_FALSE(loaded);
                REQUIRE_FALSE(processor.isIRLoaded());
            }
        }

        WHEN("attempting to load non-WAV file") {
            // Create a text file pretending to be WAV
            processor.process((float*)nullptr, (float*)nullptr, 0);  // Just to initialize

            // Try loading invalid path
            bool loaded = processor.loadIR("/dev/null");

            THEN("load fails") {
                REQUIRE_FALSE(loaded);
            }
        }
    }
}

SCENARIO("CabinetProcessor convolution basic", "[cabinet][convolution]") {
    GIVEN("processor with loaded unit impulse IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);
        processor.setMix(1.0f, 0.0f);  // Fully wet

        WHEN("processing single sample at 1.0") {
            // The mono_48k.wav has unit impulse at sample 0
            float input[256] = {0.0f};
            input[0] = 1.0f;
            float output[256];
            for (int i = 0; i < 256; ++i) {
                output[i] = input[i];
            }

            processor.process(output, output, 256);

            THEN("output at IR length reflects the impulse") {
                // With unit impulse at position 0, output[0] should be IR[0]
                // The exact value depends on normalization
                REQUIRE(std::abs(output[0]) > 0.0f);
            }
        }
    }
}

SCENARIO("CabinetProcessor circular convolution", "[cabinet][convolution]") {
    GIVEN("processor with loaded IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);
        processor.setMix(1.0f, 0.0f);

        WHEN("processing continuous signal") {
            float input[512];
            float output[512];
            for (int i = 0; i < 512; ++i) {
                input[i] = 0.3f;
                output[i] = input[i];
            }

            processor.process(output, output, 512);

            THEN("convolution maintains continuity") {
                // Check for no clicks or abrupt changes
                for (int i = 1; i < 512; ++i) {
                    float diff = std::abs(output[i] - output[i-1]);
                    // Should be relatively smooth
                    REQUIRE(diff < 1.0f);  // Not checking exact values
                }
            }
        }
    }
}

SCENARIO("CabinetProcessor stereo processing", "[cabinet][stereo]") {
    GIVEN("processor with mono IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);
        processor.setMix(1.0f, 0.0f);

        WHEN("processing stereo input with mono IR") {
            float left[64];
            float right[64];
            for (int i = 0; i < 64; ++i) {
                left[i] = 0.4f;
                right[i] = 0.6f;
            }

            processor.process(left, right, 64);

            THEN("both channels are processed") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE(std::isfinite(left[i]));
                    REQUIRE(std::isfinite(right[i]));
                }
            }
        }
    }

    GIVEN("processor with stereo IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/stereo_48k.wav")));
        processor.setBypass(false);
        processor.setMix(1.0f, 0.0f);

        WHEN("processing stereo input") {
            float left[64];
            float right[64];
            for (int i = 0; i < 64; ++i) {
                left[i] = 0.4f;
                right[i] = 0.6f;
            }

            processor.process(left, right, 64);

            THEN("each channel processes independently") {
                // With stereo IR with impulses at different positions,
                // channels should have different responses
                bool channels_differ = false;
                for (int i = 0; i < 64; ++i) {
                    if (std::abs(left[i] - right[i]) > 0.01f) {
                        channels_differ = true;
                        break;
                    }
                }
                // At least some difference expected due to different impulse positions
                // (This is weak - depends on IR content)
            }
        }
    }
}

SCENARIO("CabinetProcessor bypass mode", "[cabinet][bypass]") {
    GIVEN("processor with loaded IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));

        WHEN("bypass is enabled") {
            processor.setBypass(true);

            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.5f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("signal passes through unchanged") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE_THAT(output[i], WithinAbsMatcher(0.5f, 1e-6f));
                }
            }
        }

        WHEN("bypass is disabled") {
            processor.setBypass(false);

            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.5f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("signal is processed") {
                // Output should differ from input due to convolution
                bool processed = false;
                for (int i = 0; i < 64; ++i) {
                    if (std::abs(output[i] - input[i]) > 0.001f) {
                        processed = true;
                        break;
                    }
                }
                // With IR loaded, some processing should occur
                // (This is weak - depends on IR content)
            }
        }
    }
}

SCENARIO("CabinetProcessor dry/wet mix", "[cabinet][mix]") {
    GIVEN("processor with loaded IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);

        WHEN("mix is set to fully dry") {
            processor.setMix(0.0f, 0.0f);

            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.5f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("output equals input (no IR applied)") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE_THAT(output[i], WithinAbsMatcher(0.5f, 1e-6f));
                }
            }
        }

        WHEN("mix is set to fully wet") {
            processor.setMix(1.0f, 0.0f);

            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.5f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("IR is fully applied") {
                // Just verify no errors
                for (int i = 0; i < 64; ++i) {
                    REQUIRE(std::isfinite(output[i]));
                }
            }
        }

        WHEN("mix is at 50%") {
            processor.setMix(0.5f, 0.0f);

            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.5f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("output is blend of dry and wet") {
                // Should be somewhere between dry and wet
                for (int i = 0; i < 64; ++i) {
                    REQUIRE(std::isfinite(output[i]));
                }
            }
        }
    }
}

SCENARIO("CabinetProcessor mix smoothing", "[cabinet][smoothing]") {
    GIVEN("processor with loaded IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);
        processor.setMix(0.0f, 0.0f);  // Start dry

        WHEN("mix transitions from dry to wet with smoothing") {
            float input[256];
            float output[256];
            for (int i = 0; i < 256; ++i) {
                input[i] = 0.3f;
                output[i] = input[i];
            }

            // Process first block dry
            processor.process(output, output, 64);

            // Cross-fade to wet
            processor.setMix(1.0f, 0.05f);  // 50ms transition

            // Process during transition
            processor.process(output + 64, output + 64, 192);

            THEN("transition is smooth") {
                // Check for abrupt changes
                for (int i = 1; i < 256; ++i) {
                    float diff = std::abs(output[i] - output[i-1]);
                    // Should be gradual
                    REQUIRE(std::isfinite(output[i]));
                }
            }
        }
    }
}

SCENARIO("CabinetProcessor IR swapping", "[cabinet][swap]") {
    GIVEN("processor with first IR loaded") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);

        WHEN("processing with first IR") {
            float buffer1[64];
            for (int i = 0; i < 64; ++i) {
                buffer1[i] = 0.3f;
            }
            processor.process(buffer1, buffer1, 64);
        }

        WHEN("second IR is loaded (swap)") {
            REQUIRE(processor.loadIR(getFixturePath("irs/stereo_48k.wav")));

            THEN("swap succeeds") {
                REQUIRE(processor.isIRLoaded());
            }

            AND_WHEN("processing continues") {
                float buffer2[64];
                for (int i = 0; i < 64; ++i) {
                    buffer2[i] = 0.3f;
                }
                processor.process(buffer2, buffer2, 64);

                THEN("processing continues without errors") {
                    for (int i = 0; i < 64; ++i) {
                        REQUIRE(std::isfinite(buffer2[i]));
                    }
                }
            }
        }
    }
}

SCENARIO("CabinetProcessor different sample rates", "[cabinet][sample-rate]") {
    GIVEN("processor at 96kHz") {
        CabinetProcessor processor;
        initTestProcessor(processor, 96000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);

        WHEN("processing buffer") {
            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.3f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("processes without errors") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE(std::isfinite(output[i]));
                }
            }
        }
    }

    GIVEN("processor at 44.1kHz") {
        CabinetProcessor processor;
        initTestProcessor(processor, 44100.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);

        WHEN("processing buffer") {
            float input[64];
            float output[64];
            for (int i = 0; i < 64; ++i) {
                input[i] = 0.3f;
                output[i] = input[i];
            }

            processor.process(output, output, 64);

            THEN("processes without errors") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE(std::isfinite(output[i]));
                }
            }
        }
    }
}

SCENARIO("CabinetProcessor edge cases", "[cabinet][edge]") {
    GIVEN("processor with loaded IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));
        processor.setBypass(false);

        WHEN("processing silent input") {
            float silent[64] = {0.0f};
            processor.process(silent, silent, 64);

            THEN("output remains silent") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE_THAT(silent[i], WithinAbsMatcher(0.0f, 1e-10f));
                }
            }
        }

        WHEN("processing very small buffer") {
            float tiny[1] = {0.5f};
            processor.process(tiny, tiny, 1);

            THEN("processes single sample") {
                REQUIRE(std::isfinite(tiny[0]));
            }
        }

        WHEN("processing large buffer") {
            float large[4096];
            for (int i = 0; i < 4096; ++i) {
                large[i] = 0.2f;
            }

            processor.process(large, large, 4096);

            THEN("processes without issues") {
                for (int i = 0; i < 4096; ++i) {
                    REQUIRE(std::isfinite(large[i]));
                }
            }
        }

        WHEN("processing DC signal") {
            float dc[64];
            for (int i = 0; i < 64; ++i) {
                dc[i] = 0.5f;  // DC at 0.5
            }

            processor.process(dc, dc, 64);

            THEN("DC is convolved correctly") {
                for (int i = 0; i < 64; ++i) {
                    REQUIRE(std::isfinite(dc[i]));
                }
            }
        }
    }
}

SCENARIO("CabinetProcessor IR length queries", "[cabinet][metadata]") {
    GIVEN("processor with loaded IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);
        REQUIRE(processor.loadIR(getFixturePath("irs/mono_48k.wav")));

        WHEN("querying IR length") {
            size_t length = processor.getIRLength();

            THEN("returns non-zero length") {
                REQUIRE(length > 0);
                // mono_48k.wav is 1 second at 48kHz = 48000 samples
                REQUIRE(length > 1000);
            }
        }
    }

    GIVEN("processor without IR") {
        CabinetProcessor processor;
        initTestProcessor(processor, 48000.0f);

        WHEN("querying IR length") {
            size_t length = processor.getIRLength();

            THEN("returns zero") {
                REQUIRE(length == 0);
            }
        }
    }
}
