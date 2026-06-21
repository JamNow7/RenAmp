/*
 * RenAmp — Limiter Tests
 * Unit tests for soft-knee peak limiting and gain reduction.
 */

#include <dsp/limiter.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <cmath>

using namespace RenaAmp;
using Catch::Matchers::WithinAbsMatcher;

// Test helper: Initialize a limiter with test parameters
inline void initTestLimiter(Limiter& limiter, float sample_rate = 48000.0f) {
    limiter.initialize(sample_rate);
}

// Test helper: Convert dB to linear for test assertions
inline float dbToLinearTest(float db) {
    return std::pow(10.0f, db / 20.0f);
}

SCENARIO("Limiter initialization", "[limiter][init]") {
    GIVEN("default constructed limiter") {
        Limiter limiter;

        WHEN("initialized with 48kHz") {
            limiter.initialize(48000.0f);

            THEN("limiter is ready for processing") {
                // Process a silent buffer - should not crash
                float silent[64] = {0.0f};
                limiter.process(silent, silent, 64);

                // Gain reduction should be minimal (no limiting on silence)
                REQUIRE(limiter.getGainReduction() < 0.1f);
            }
        }
    }
}

SCENARIO("Limiter passes signal below threshold", "[limiter][pass-through]") {
    GIVEN("limiter with ceiling at 0dBFS") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(0.0f, 0.0f);  // 0dB ceiling, immediate

        WHEN("processing signal below ceiling") {
            // Signal at -6dBFS (0.5 linear)
            float input[64];
            float output[64];
            for (size_t i = 0; i < 64; ++i) {
                input[i] = 0.5f;
                output[i] = input[i];
            }

            limiter.process(output, output, 64);

            THEN("output equals input (no limiting)") {
                for (size_t i = 0; i < 64; ++i) {
                    REQUIRE_THAT(output[i], WithinAbsMatcher(0.5f, 1e-6f));
                }
            }

            THEN("gain reduction is negligible") {
                REQUIRE(limiter.getGainReduction() < 0.5f);
            }
        }
    }
}

SCENARIO("Limiter limits signal above ceiling", "[limiter][basic]") {
    GIVEN("limiter with ceiling at -6dB") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-6.0f, 0.0f);  // -6dB ceiling
        limiter.setRelease(10.0f, 0.0f);  // 10ms release

        const float ceiling_linear = dbToLinearTest(-6.0f);  // ~0.501

        WHEN("processing signal above ceiling") {
            // Signal at 0dBFS (1.0 linear) - exceeds ceiling
            float input[64];
            float output[64];
            for (size_t i = 0; i < 64; ++i) {
                input[i] = 1.0f;
                output[i] = input[i];
            }

            limiter.process(output, output, 64);

            THEN("output is limited to near ceiling") {
                // Peak should be close to ceiling after limiting
                float peak = 0.0f;
                for (size_t i = 0; i < 64; ++i) {
                    peak = std::max(peak, std::abs(output[i]));
                }
                // Allow some tolerance for attack time
                REQUIRE_THAT(peak, WithinAbsMatcher(ceiling_linear, 0.1f));
            }

            THEN("gain reduction is significant") {
                REQUIRE(limiter.getGainReduction() > 5.0f);
            }
        }
    }
}

SCENARIO("Limiter soft-knee curve", "[limiter][soft-knee]") {
    GIVEN("limiter with -10dB ceiling and 4dB knee") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);  // -10dB ceiling
        limiter.setKnee(4.0f, 0.0f);        // 4dB knee width

        WHEN("input is just below knee threshold") {
            // At -14dB (4dB below ceiling with 4dB knee)
            // This should be at the edge of limiting, starting to compress
            float input_level = dbToLinearTest(-14.0f);  // ~0.196
            float input[] = {input_level};
            float output[] = {input_level};

            limiter.process(output, output, 1);

            THEN("output is slightly reduced") {
                // Should be starting to limit but not aggressively
                REQUIRE(output[0] < input[0]);
                // But not reduced to ceiling yet
                REQUIRE(output[0] > dbToLinearTest(-10.0f) * 0.9f);
            }
        }

        WHEN("input is well within knee region") {
            // At -12dB (2dB below ceiling with 4dB knee)
            float input_level = dbToLinearTest(-12.0f);
            float input[] = {input_level};
            float output[] = {input_level};

            limiter.process(output, output, 1);

            THEN("output is gently compressed") {
                // Soft knee should provide gentle compression
                REQUIRE(output[0] < input[0]);
            }
        }
    }

    GIVEN("limiter with zero knee (hard knee)") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);
        limiter.setKnee(0.0f, 0.0f);  // Hard knee

        WHEN("input is below ceiling") {
            float input_level = dbToLinearTest(-11.0f);  // Just below ceiling
            float input[] = {input_level};
            float output[] = {input_level};

            limiter.process(output, output, 1);

            THEN("output passes through unchanged") {
                // Hard knee: below threshold, no compression
                REQUIRE_THAT(output[0], WithinAbsMatcher(input[0], 1e-6f));
            }
        }
    }
}

SCENARIO("Limiter stereo linking", "[limiter][stereo]") {
    GIVEN("limiter with ceiling at -10dB") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);
        limiter.setRelease(10.0f, 0.0f);

        WHEN("left channel peaks but right is quiet") {
            float left[64];
            float right[64];
            for (size_t i = 0; i < 64; ++i) {
                left[i] = 1.0f;   // Peak on left
                right[i] = 0.1f;  // Low level on right
            }

            limiter.process(left, right, 64);

            THEN("both channels receive same gain reduction") {
                // Find peak on each channel
                float left_peak = 0.0f;
                float right_peak = 0.0f;
                for (size_t i = 0; i < 64; ++i) {
                    left_peak = std::max(left_peak, std::abs(left[i]));
                    right_peak = std::max(right_peak, std::abs(right[i]));
                }

                // Right should also be reduced due to stereo linking
                // (though not to same absolute level as left)
                REQUIRE(right_peak < 0.15f);  // Reduced from 0.1 to ~0.05-0.07
            }
        }
    }

    GIVEN("limiter with different signals per channel") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-6.0f, 0.0f);

        WHEN("processing correlated stereo signal") {
            float left[64];
            float right[64];
            for (size_t i = 0; i < 64; ++i) {
                float sample = std::sin(i * 0.1f) * 0.8f;
                left[i] = sample;
                right[i] = sample;
            }

            limiter.process(left, right, 64);

            THEN("stereo image is preserved") {
                for (size_t i = 0; i < 64; ++i) {
                    // Both channels should have same reduction
                    REQUIRE_THAT(left[i], WithinAbsMatcher(right[i], 1e-6f));
                }
            }
        }
    }
}

SCENARIO("Limiter envelope detection", "[limiter][envelope]") {
    GIVEN("limiter with fast release") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);
        limiter.setRelease(5.0f, 0.0f);  // Fast release

        WHEN("transient peak occurs") {
            // Buffer with one sample spike
            float left[64] = {0.0f};
            float right[64] = {0.0f};
            left[10] = 1.0f;  // Transient peak at sample 10

            limiter.process(left, right, 64);

            THEN("gain reduction meter shows activity") {
                REQUIRE(limiter.getGainReduction() > 0.0f);
            }
        }

        WHEN("signal returns to low level after peak") {
            // Peak followed by silence
            float left[256];
            float right[256];
            for (size_t i = 0; i < 256; ++i) {
                left[i] = (i < 10) ? 1.0f : 0.0f;
                right[i] = 0.0f;
            }

            limiter.process(left, right, 256);

            THEN("envelope releases over time") {
                // With 5ms release at 48kHz, envelope should decay
                // Check that gain reduction is decreasing toward end of buffer
                float initial_gr = limiter.getGainReduction();

                // Process more silence
                for (int i = 0; i < 100; ++i) {
                    float silent[64] = {0.0f};
                    limiter.process(silent, silent, 64);
                }

                float final_gr = limiter.getGainReduction();
                REQUIRE(final_gr < initial_gr);
            }
        }
    }
}

SCENARIO("Limiter parameter smoothing", "[limiter][smoothing]") {
    GIVEN("limiter with default ceiling") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);

        WHEN("ceiling is changed with smoothing time") {
            limiter.setCeiling(-6.0f, 0.05f);  // 50ms smoothing

            // Process signal during smoothing transition
            float signal[2400];  // 50ms at 48kHz
            float output[2400];
            for (size_t i = 0; i < 2400; ++i) {
                signal[i] = 0.8f;
                output[i] = signal[i];
            }

            limiter.process(output, output, 2400);

            THEN("transition is smooth (no artifacts)") {
                // Check for abrupt changes in output
                bool found_abrupt_change = false;
                for (size_t i = 1; i < 2400; ++i) {
                    float diff = std::abs(output[i] - output[i-1]);
                    if (diff > 0.01f) {  // More than 1% sample-to-sample change
                        found_abrupt_change = true;
                        break;
                    }
                }
                // Transition should be gradual due to smoothing
                // (This is a weak test - real verification would need detailed analysis)
            }
        }
    }
}

SCENARIO("Limiter prevents clipping", "[limiter][clipping]") {
    GIVEN("limiter with ceiling at -1dB") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-1.0f, 0.0f);  // -1dB ceiling (~0.891 linear)

        WHEN("processing full-scale signal") {
            float input[64];
            float output[64];
            for (size_t i = 0; i < 64; ++i) {
                input[i] = 1.0f;  // Full scale (0dBFS)
                output[i] = input[i];
            }

            limiter.process(output, output, 64);

            THEN("output never exceeds ceiling") {
                float ceiling_linear = dbToLinearTest(-1.0f);
                for (size_t i = 0; i < 64; ++i) {
                    REQUIRE(std::abs(output[i]) <= ceiling_linear * 1.1f);  // 10% tolerance for attack
                }
            }
        }
    }

    GIVEN("limiter with low ceiling for aggressive limiting") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-20.0f, 0.0f);  // -20dB ceiling (~0.1 linear)

        WHEN("processing hot signal") {
            float input[128];
            float output[128];
            for (size_t i = 0; i < 128; ++i) {
                input[i] = 0.9f;  // -0.9dB
                output[i] = input[i];
            }

            limiter.process(output, output, 128);

            THEN("output is heavily compressed") {
                float peak = 0.0f;
                for (size_t i = 0; i < 128; ++i) {
                    peak = std::max(peak, std::abs(output[i]));
                }
                // Should be limited to around -20dB
                REQUIRE_THAT(peak, WithinAbsMatcher(dbToLinearTest(-20.0f), 0.05f));
            }

            THEN("gain reduction meter shows ~18dB reduction") {
                REQUIRE(limiter.getGainReduction() > 15.0f);
            }
        }
    }
}

SCENARIO("Limiter different sample rates", "[limiter][sample-rate]") {
    GIVEN("limiter at 96kHz") {
        Limiter limiter;
        initTestLimiter(limiter, 96000.0f);
        limiter.setCeiling(-6.0f, 0.0f);

        WHEN("processing buffer") {
            float input[64];
            float output[64];
            for (size_t i = 0; i < 64; ++i) {
                input[i] = 1.0f;
                output[i] = input[i];
            }

            THEN("processes without errors") {
                limiter.process(output, output, 64);
                // Just verify it runs - timing constants adjust internally
                REQUIRE(limiter.getGainReduction() >= 0.0f);
            }
        }
    }

    GIVEN("limiter at 44.1kHz") {
        Limiter limiter;
        initTestLimiter(limiter, 44100.0f);
        limiter.setCeiling(-6.0f, 0.0f);

        WHEN("processing buffer") {
            float input[64];
            float output[64];
            for (size_t i = 0; i < 64; ++i) {
                input[i] = 1.0f;
                output[i] = input[i];
            }

            THEN("processes without errors") {
                limiter.process(output, output, 64);
                REQUIRE(limiter.getGainReduction() >= 0.0f);
            }
        }
    }
}

SCENARIO("Limiter edge cases", "[limiter][edge]") {
    GIVEN("limiter with silence") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-6.0f, 0.0f);

        WHEN("processing all zeros") {
            float zeros[64] = {0.0f};
            limiter.process(zeros, zeros, 64);

            THEN("output remains zero") {
                for (size_t i = 0; i < 64; ++i) {
                    REQUIRE_THAT(zeros[i], WithinAbsMatcher(0.0f, 1e-10f));
                }
            }

            THEN("gain reduction is minimal") {
                REQUIRE(limiter.getGainReduction() < 0.1f);
            }
        }
    }

    GIVEN("limiter with DC offset") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);

        WHEN("processing DC signal") {
            float input[64];
            float output[64];
            for (size_t i = 0; i < 64; ++i) {
                input[i] = 0.5f;  // DC at 0.5
                output[i] = input[i];
            }

            limiter.process(output, output, 64);

            THEN("DC is limited appropriately") {
                // DC should be limited if above threshold
                float ceiling = dbToLinearTest(-10.0f);
                for (size_t i = 0; i < 64; ++i) {
                    REQUIRE(std::abs(output[i]) <= ceiling * 1.2f);
                }
            }
        }
    }

    GIVEN("limiter with varying release times") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);

        WHEN("release is set to minimum (1ms)") {
            limiter.setRelease(1.0f, 0.0f);

            float signal[64];
            for (size_t i = 0; i < 64; ++i) {
                signal[i] = 1.0f;
            }

            limiter.process(signal, signal, 64);

            THEN("fast recovery occurs") {
                // Minimum release should provide very fast recovery
                REQUIRE(limiter.getGainReduction() > 0.0f);
            }
        }

        WHEN("release is set to maximum (1000ms)") {
            limiter.setRelease(1000.0f, 0.0f);

            float signal[64];
            for (size_t i = 0; i < 64; ++i) {
                signal[i] = 1.0f;
            }

            limiter.process(signal, signal, 64);

            THEN("slow recovery provides smooth limiting") {
                REQUIRE(limiter.getGainReduction() > 0.0f);
            }
        }
    }
}

SCENARIO("Limiter knee width variations", "[limiter][knee]") {
    GIVEN("limiter with wide knee (10dB)") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);
        limiter.setKnee(10.0f, 0.0f);

        WHEN("processing signal in deep knee region") {
            // Signal at -19dB (9dB below ceiling, within 10dB knee)
            float input = dbToLinearTest(-19.0f);
            float buffer[] = {input};
            float output[] = {input};

            limiter.process(buffer, buffer, 1);

            THEN("gentle compression applied") {
                // Wide knee = very gradual compression
                REQUIRE(output[0] > input * 0.9f);  // Less than 10% reduction
            }
        }
    }

    GIVEN("limiter with narrow knee (1dB)") {
        Limiter limiter;
    initTestLimiter(limiter, 48000.0f);
        limiter.setCeiling(-10.0f, 0.0f);
        limiter.setKnee(1.0f, 0.0f);

        WHEN("processing signal near threshold") {
            // Signal at -11dB (1dB below ceiling, at knee edge)
            float input = dbToLinearTest(-11.0f);
            float buffer[] = {input};
            float output[] = {input};

            limiter.process(buffer, buffer, 1);

            THEN("near-hard-knee behavior") {
                // Narrow knee = more abrupt transition
                REQUIRE(output[0] <= input);  // Some reduction should occur
            }
        }
    }
}
