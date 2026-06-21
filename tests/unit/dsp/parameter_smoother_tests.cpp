/*
 * RenAmp — ParameterSmoother Tests
 * Unit tests for cross-thread parameter smoothing and linear interpolation.
 */

#include <parameters/param_smoother.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <thread>
#include <chrono>
#include <vector>

using namespace RenaAmp;
using Catch::Matchers::WithinAbsMatcher;

// Test helper: Initialize a smoother with test parameters
inline void initTestSmoother(ParameterSmoother& smoother, float sample_rate = 48000.0f, float initial_value = 0.0f) {
    smoother.init(sample_rate, initial_value);
}

SCENARIO("ParameterSmoother initialization", "[smoother][init]") {
    GIVEN("default constructed smoother") {
        ParameterSmoother smoother;

        WHEN("initialized with 48kHz and initial value 0.5") {
            smoother.init(48000.0f, 0.5f);

            THEN("current value equals initial value") {
                REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(0.5f, 1e-6f));
            }

            THEN("smoother is not actively smoothing") {
                REQUIRE_FALSE(smoother.isSmoothing());
            }
        }
    }

    GIVEN("smoother initialized with default parameters") {
        ParameterSmoother smoother;
        smoother.init(48000.0f);

        THEN("current value is 0.0 (default)") {
            REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(0.0f, 1e-6f));
        }
    }
}

SCENARIO("ParameterSmoother linear interpolation", "[smoother][interpolation]") {
    GIVEN("smoother at 0.0, targeting 1.0 with 0.1s duration at 48kHz") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.1f);

        const int expected_samples = 4800;  // 0.1s * 48000 samples/s

        WHEN("processing for exactly the ramp duration") {
            float final_value = 0.0f;
            for (int i = 0; i < expected_samples; ++i) {
                final_value = smoother.next();
            }

            THEN("final value equals target (1.0)") {
                REQUIRE_THAT(final_value, WithinAbsMatcher(1.0f, 1e-6f));
            }

            THEN("smoother is no longer actively smoothing") {
                REQUIRE_FALSE(smoother.isSmoothing());
            }
        }

        WHEN("processing for half the ramp duration") {
            float mid_value = 0.0f;
            for (int i = 0; i < expected_samples / 2; ++i) {
                mid_value = smoother.next();
            }

            THEN("value is exactly halfway (0.5)") {
                REQUIRE_THAT(mid_value, WithinAbsMatcher(0.5f, 0.01f));
            }

            THEN("smoother is still actively smoothing") {
                REQUIRE(smoother.isSmoothing());
            }
        }

        WHEN("processing for one quarter of the ramp duration") {
            float quarter_value = 0.0f;
            for (int i = 0; i < expected_samples / 4; ++i) {
                quarter_value = smoother.next();
            }

            THEN("value is approximately 0.25") {
                REQUIRE_THAT(quarter_value, WithinAbsMatcher(0.25f, 0.01f));
            }
        }
    }

    GIVEN("smoother ramping downward from 1.0 to 0.0") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 1.0f);
        smoother.setTarget(0.0f, 0.05f);  // 2400 samples

        WHEN("processing for full duration") {
            float final_value = 0.0f;
            for (int i = 0; i < 2400; ++i) {
                final_value = smoother.next();
            }

            THEN("reaches target 0.0") {
                REQUIRE_THAT(final_value, WithinAbsMatcher(0.0f, 1e-6f));
            }
        }
    }

    GIVEN("smoother ramping between arbitrary values") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.3f);
        smoother.setTarget(0.8f, 0.1f);  // 4800 samples

        WHEN("processing for full duration") {
            float final_value = 0.0f;
            for (int i = 0; i < 4800; ++i) {
                final_value = smoother.next();
            }

            THEN("reaches target 0.8") {
                REQUIRE_THAT(final_value, WithinAbsMatcher(0.8f, 1e-6f));
            }

            THEN("total delta is 0.5") {
                REQUIRE_THAT(final_value - 0.3f, WithinAbsMatcher(0.5f, 1e-6f));
            }
        }
    }
}

SCENARIO("ParameterSmoother immediate target (zero duration)", "[smoother][immediate]") {
    GIVEN("smoother at 0.0") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);

        WHEN("target set with zero duration") {
            smoother.setTarget(1.0f, 0.0f);

            THEN("next() returns target immediately") {
                REQUIRE_THAT(smoother.next(), WithinAbsMatcher(1.0f, 1e-6f));
            }

            THEN("smoother is not actively smoothing") {
                REQUIRE_FALSE(smoother.isSmoothing());
            }
        }
    }
}

SCENARIO("ParameterSmoother immediate reset", "[smoother][reset]") {
    GIVEN("smoother ramping from 0 to 1 over 0.1s") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.1f);

        WHEN("processing for half duration then reset to 2.0") {
            for (int i = 0; i < 2400; ++i) {
                smoother.next();
            }

            smoother.reset(2.0f);

            THEN("value immediately becomes 2.0") {
                REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(2.0f, 1e-6f));
            }

            THEN("no active smoothing") {
                REQUIRE_FALSE(smoother.isSmoothing());
            }

            THEN("subsequent next() calls maintain 2.0") {
                REQUIRE_THAT(smoother.next(), WithinAbsMatcher(2.0f, 1e-6f));
                REQUIRE_THAT(smoother.next(), WithinAbsMatcher(2.0f, 1e-6f));
            }
        }
    }

    GIVEN("smoother with active ramp") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.1f);

        WHEN("reset is called") {
            smoother.reset(0.5f);

            THEN("isSmoothing returns false") {
                REQUIRE_FALSE(smoother.isSmoothing());
            }
        }
    }
}

SCENARIO("ParameterSmoother consecutive ramps", "[smoother][consecutive]") {
    GIVEN("smoother completing first ramp") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.05f);  // 2400 samples

        for (int i = 0; i < 2400; ++i) {
            smoother.next();
        }

        REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(1.0f, 1e-6f));

        WHEN("new target is set from current position") {
            smoother.setTarget(0.0f, 0.05f);  // Back to 0.0

            THEN("ramp starts from current value (1.0), not initial") {
                // After half duration (1200 samples), should be at 0.5
                for (int i = 0; i < 1200; ++i) {
                    smoother.next();
                }
                REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(0.5f, 0.01f));
            }
        }
    }
}

SCENARIO("ParameterSmoother cross-thread atomic updates", "[smoother][atomic]") {
    GIVEN("smoother being processed by audio thread") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        std::atomic<bool> audio_done{false};
        std::atomic<bool> control_should_update{false};
        std::vector<float> audio_outputs;

        // Simulate audio thread
        std::thread audio_thread([&]() {
            for (int i = 0; i < 10000; ++i) {
                audio_outputs.push_back(smoother.next());
                if (i == 2000) {
                    control_should_update = true;
                }
                if (i >= 3000) {
                    // Check if we've picked up the update
                    if (audio_outputs.size() > 2500) {
                        // Somewhere after sample 2500, value should start changing
                        if (smoother.getCurrent() > 0.01f) {
                            break;
                        }
                    }
                }
                if (audio_outputs.size() >= 10000) break;
            }
            audio_done = true;
        });

        // Simulate control thread
        std::thread control_thread([&]() {
            while (!control_should_update) {
                std::this_thread::yield();
            }
            // Simulate control thread setting new target
            smoother.setTarget(1.0f, 0.05f);  // Ramp from current to 1.0 over 2400 samples
        });

        audio_thread.join();
        control_thread.join();

        THEN("audio thread eventually picks up the control thread update") {
            // Find first sample where value > 0.01 (update took effect)
            bool found_increase = false;
            for (size_t i = 2000; i < audio_outputs.size(); ++i) {
                if (audio_outputs[i] > 0.01f) {
                    found_increase = true;
                    break;
                }
            }
            REQUIRE(found_increase);
        }
    }

    GIVEN("smoother with rapid control thread updates") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);

        WHEN("control thread sets multiple targets quickly") {
            smoother.setTarget(1.0f, 0.1f);   // First target
            smoother.setTarget(0.5f, 0.05f);  // Override target
            smoother.setTarget(0.75f, 0.025f); // Override again

            THEN("last setTarget wins (audio thread processes final target)") {
                // Process for duration of last ramp (0.025s * 48000 = 1200 samples)
                float final_value = 0.0f;
                for (int i = 0; i < 1500; ++i) {
                    final_value = smoother.next();
                }

                // Should converge to last target set (0.75)
                REQUIRE_THAT(final_value, WithinAbsMatcher(0.75f, 0.02f));
            }
        }
    }
}

SCENARIO("ParameterSmoother step calculation accuracy", "[smoother][step]") {
    GIVEN("smoother at 0.0, target 1.0, 0.1s at 48kHz") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.1f);

        // Expected step: 1.0 / 4800 = ~0.000208333
        const float expected_step = 1.0f / 4800.0f;

        WHEN("processing single samples") {
            float v0 = smoother.getCurrent();  // Should be 0.0
            float v1 = smoother.next();        // v0 + step
            float v2 = smoother.next();        // v1 + step

            THEN("each sample increases by expected step") {
                REQUIRE_THAT(v1 - v0, WithinAbsMatcher(expected_step, 1e-8f));
                REQUIRE_THAT(v2 - v1, WithinAbsMatcher(expected_step, 1e-8f));
            }
        }
    }

    GIVEN("smoother at different sample rates") {
        WHEN("sample rate is 96kHz") {
            ParameterSmoother smoother;
        initTestSmoother(smoother, 96000.0f, 0.0f);
            smoother.setTarget(1.0f, 0.1f);  // 9600 samples

            const float expected_step = 1.0f / 9600.0f;
            float v0 = smoother.getCurrent();
            float v1 = smoother.next();

            THEN("step is smaller due to higher sample rate") {
                REQUIRE_THAT(v1 - v0, WithinAbsMatcher(expected_step, 1e-8f));
            }
        }

        WHEN("sample rate is 44.1kHz") {
            ParameterSmoother smoother;
        initTestSmoother(smoother, 44100.0f, 0.0f);
            smoother.setTarget(1.0f, 0.1f);  // 4410 samples

            const float expected_step = 1.0f / 4410.0f;
            float v0 = smoother.getCurrent();
            float v1 = smoother.next();

            THEN("step matches 44.1kHz calculation") {
                REQUIRE_THAT(v1 - v0, WithinAbsMatcher(expected_step, 1e-8f));
            }
        }
    }
}

SCENARIO("ParameterSmoother getCurrent() does not advance state", "[smoother][getCurrent]") {
    GIVEN("smoother ramping from 0 to 1") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.1f);

        WHEN("getCurrent() is called multiple times without next()") {
            float v1 = smoother.getCurrent();
            float v2 = smoother.getCurrent();
            float v3 = smoother.getCurrent();

            THEN("all values are identical (no advancement)") {
                REQUIRE(v1 == v2);
                REQUIRE(v2 == v3);
                REQUIRE_THAT(v1, WithinAbsMatcher(0.0f, 1e-6f));
            }
        }

        WHEN("getCurrent() is called after next()") {
            float v_next = smoother.next();
            float v_current = smoother.getCurrent();

            THEN("getCurrent() reflects the value after next()") {
                REQUIRE_THAT(v_current, WithinAbsMatcher(v_next, 1e-8f));
            }
        }
    }
}

SCENARIO("MultiParameterSmoother basic operation", "[smoother][multi]") {
    GIVEN("multi-channel smoother initialized with 2 channels") {
        MultiParameterSmoother multi;
        multi.init(48000.0f, 2, 0.0f);

        WHEN("target is set for all channels") {
            multi.setTargetAll(1.0f, 0.1f);

            THEN("all channels advance together") {
                const float* values = nullptr;
                for (int i = 0; i < 2400; ++i) {
                    values = multi.next();
                }

                REQUIRE_THAT(values[0], WithinAbsMatcher(0.5f, 0.01f));
                REQUIRE_THAT(values[1], WithinAbsMatcher(0.5f, 0.01f));
            }
        }
    }

    GIVEN("multi-channel smoother with different targets per channel") {
        MultiParameterSmoother multi;
        multi.init(48000.0f, 2, 0.0f);

        WHEN("channel 0 targets 0.5, channel 1 targets 1.0") {
            multi.setTargetChannel(0, 0.5f, 0.1f);
            multi.setTargetChannel(1, 1.0f, 0.1f);

            const float* values = nullptr;
            for (int i = 0; i < 4800; ++i) {
                values = multi.next();
            }

            THEN("each channel reaches its respective target") {
                REQUIRE_THAT(values[0], WithinAbsMatcher(0.5f, 1e-6f));
                REQUIRE_THAT(values[1], WithinAbsMatcher(1.0f, 1e-6f));
            }
        }
    }

    GIVEN("multi-channel smoother") {
        MultiParameterSmoother multi;
        multi.init(48000.0f, 4, 0.0f);

        WHEN("all channels are reset") {
            multi.setTargetAll(1.0f, 0.1f);
            for (int i = 0; i < 2400; ++i) {
                multi.next();
            }

            multi.resetAll(0.5f);

            THEN("all channels immediately have reset value") {
                REQUIRE_THAT(multi.getChannel(0), WithinAbsMatcher(0.5f, 1e-6f));
                REQUIRE_THAT(multi.getChannel(1), WithinAbsMatcher(0.5f, 1e-6f));
                REQUIRE_THAT(multi.getChannel(2), WithinAbsMatcher(0.5f, 1e-6f));
                REQUIRE_THAT(multi.getChannel(3), WithinAbsMatcher(0.5f, 1e-6f));
            }
        }
    }
}

SCENARIO("ParameterSmoother edge cases", "[smoother][edge]") {
    GIVEN("smoother with very short duration") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.0f);
        smoother.setTarget(1.0f, 0.0001f);  // 4.8 samples (rounds to ~5)

        WHEN("processing for rounded-up duration") {
            for (int i = 0; i < 10; ++i) {
                smoother.next();
            }

            THEN("still reaches target") {
                REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(1.0f, 1e-6f));
            }
        }
    }

    GIVEN("smoother with zero-delta ramp") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, 0.5f);
        smoother.setTarget(0.5f, 0.1f);  // Same value

        WHEN("processing any number of samples") {
            smoother.next();
            smoother.next();
            smoother.next();

            THEN("value remains constant") {
                // Value should be at target (0.5)
                REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(0.5f, 1e-6f));
                // Note: isSmoothing() behavior may vary for zero-delta ramps
                // The important thing is the value is correct
            }
        }
    }

    GIVEN("smoother with large target delta") {
        ParameterSmoother smoother;
        initTestSmoother(smoother, 48000.0f, -100.0f);
        smoother.setTarget(100.0f, 0.1f);  // Delta of 200

        WHEN("processing for full duration") {
            for (int i = 0; i < 4800; ++i) {
                smoother.next();
            }

            THEN("reaches large target accurately") {
                REQUIRE_THAT(smoother.getCurrent(), WithinAbsMatcher(100.0f, 1e-6f));
            }
        }
    }
}
