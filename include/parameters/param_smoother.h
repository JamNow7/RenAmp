/*
 * Renamp — Parameter Smoother
 * Purpose: Cross-thread parameter ramping to prevent zipper noise from abrupt changes.
 * Real-time safety: next() is RT-safe (no allocations, locks, or I/O).
 *                  setTarget() is RT-safe and can be called from control thread.
 * Threading model: Control thread calls setTarget(); audio thread calls next().
 *                 Cross-thread communication via atomic pending_update flag and data.
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace RenaAmp {

/**
 * @brief RT-safe parameter smoother for zipper-noise prevention
 *
 * Provides smooth parameter transitions (linear ramps) to prevent clicks
 * and zipper noise when parameters change abruptly.
 *
 * Uses atomic operations for lock-free parameter updates from control thread.
 * The audio thread consumes smoothed values via next() at sample rate.
 *
 * Threading:
 * - Control thread: setTarget() writes to atomic pending_update flag and data
 * - Audio thread: next() reads atomic flag, processes pending updates, returns smoothed value
 *
 * Cross-thread communication:
 * - Single-producer (control thread) / single-consumer (audio thread)
 * - Lock-free via atomic bool flag
 * - No mutexes or priority inversion issues
 *
 * Usage example:
 *   // Initialization (setup)
 *   smoother.init(48000.0f, 0.0f);  // sample_rate, initial_value
 *
 *   // From control thread (any time)
 *   smoother.setTarget(1.0f, 0.05f);  // target_value, duration_seconds
 *
 *   // From audio thread (once per sample)
 *   float value = smoother.next();
 */
class ParameterSmoother {
public:
    ParameterSmoother();
    ~ParameterSmoother() = default;

    /**
     * @brief Initialize smoother with sample rate and initial value
     * @param sample_rate Sample rate in Hz (> 0)
     * @param initial_value Starting value (default 0.0f)
     * @post Smoother ready for use; current value set to initial_value
     * @note Call during setup; not thread-safe to call concurrently with setTarget()
     */
    void init(float sample_rate, float initial_value = 0.0f);

    /**
     * @brief Set new target value with smoothing duration
     * @param target Target value to ramp toward
     * @param duration_seconds Smoothing duration in seconds (0 = immediate)
     * @post Target scheduled for smoothing; takes effect on next next() call
     * @note Thread-safe: can be called from control thread while audio thread calls next()
     * @note Uses atomic operations; no locks or priority inversion
     */
    void setTarget(float target, float duration_seconds);

    /**
     * @brief Get next smoothed value (call once per sample)
     * @return Current smoothed value (advances toward target)
     * @pre init() was called
     * @post Internal state advanced by one sample step
     * @note RT-Safe: no allocations, locks, or I/O; callable from audio thread
     * @note Checks for pending updates from control thread via atomic flag
     */
    float next();

    /**
     * @brief Get current value without advancing state
     * @return Current smoothed value
     * @note Useful for UI metering/inspection; does not process pending updates
     */
    float getCurrent() const;

    /**
     * @brief Reset to specific value immediately (no smoothing)
     * @param value Value to reset to
     * @post current_ and target_ set to value; smoothing canceled
     * @note Useful for preset changes or immediate parameter responses
     */
    void reset(float value);

    /**
     * @brief Check if smoothing is currently active
     * @return true if ramping toward target (samples_remaining > 0)
     * @note RT-Safe: reads internal state only
     */
    bool isSmoothing() const;

private:
    // Audio-thread owned state (modified only in next())
    float current_{0.0f};                ///< Current output value
    float target_{0.0f};                 ///< Target value
    float step_{0.0f};                   ///< Step size per sample
    int32_t samples_remaining_{0};       ///< Samples remaining in ramp

    // Sample rate for time-to-sample conversion
    float sample_rate_{48000.0f};        ///< Sample rate in Hz

    // Cross-thread communication: control thread writes, audio thread reads
    struct TargetUpdate {
        float target;                    ///< Target value from control thread
        float duration_seconds;          ///< Smoothing duration from control thread
    };

    std::atomic<bool> pending_update_{false};  ///< Flag indicating new target available
    TargetUpdate pending_update_data_;           ///< Data written by control thread

    /**
     * @brief Process pending update from control thread
     * Called internally from next() when pending_update_ flag is set
     * @post pending_update_ cleared; ramp parameters calculated
     */
    void processPendingUpdate();

    /**
     * @brief Calculate step size for linear ramp
     * @param from Starting value
     * @param to Target value
     * @param duration_samples Ramp duration in samples
     * @return Step size per sample (linear interpolation)
     */
    float calculateStep(float from, float to, float duration_samples);
};

/**
 * @brief Multi-channel parameter smoother
 *
 * Smoothes multiple related parameters together (useful for stereo or multi-bus).
 * More efficient than managing multiple individual smoothers.
 *
 * Contains an array of ParameterSmoother instances and provides batch operations.
 */
class MultiParameterSmoother {
public:
    static constexpr size_t MAX_CHANNELS = 8;

    MultiParameterSmoother();
    ~MultiParameterSmoother() = default;

    /**
     * @brief Initialize all channels
     * @param sample_rate Sample rate in Hz (> 0)
     * @param num_channels Number of channels to use (1 to MAX_CHANNELS)
     * @param initial_value Starting value for all channels (default 0.0f)
     * @post Specified number of channels initialized and ready
     */
    void init(float sample_rate, size_t num_channels, float initial_value = 0.0f);

    /**
     * @brief Set target for all channels
     * @param target Target value for all channels
     * @param duration_seconds Smoothing duration in seconds
     * @note Thread-safe: can be called from control thread
     */
    void setTargetAll(float target, float duration_seconds);

    /**
     * @brief Set target for specific channel
     * @param channel Channel index (0 to num_channels-1)
     * @param target Target value
     * @param duration_seconds Smoothing duration in seconds
     * @note Thread-safe: can be called from control thread
     */
    void setTargetChannel(size_t channel, float target, float duration_seconds);

    /**
     * @brief Get next smoothed values for all channels
     * @return Pointer to internal buffer containing current values
     * @pre init() was called
     * @post All channels advanced by one sample step
     * @note RT-Safe: no allocations; callable from audio thread
     */
    const float* next();

    /**
     * @brief Get value for specific channel without advancing
     * @param channel Channel index (0 to num_channels-1)
     * @return Current value for channel
     * @note Useful for UI metering; does not advance state
     */
    float getChannel(size_t channel) const;

    /**
     * @brief Reset all channels to specific value
     * @param value Value to reset all channels to
     * @post All channels reset; active smoothing canceled
     */
    void resetAll(float value);

private:
    ParameterSmoother smoothers_[MAX_CHANNELS];  ///< Individual smoothers per channel
    size_t num_channels_{0};                      ///< Active number of channels
    float values_[MAX_CHANNELS];                   ///< Output buffer
};

} // namespace RenaAmp
