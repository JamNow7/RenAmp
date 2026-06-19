/*
 * RenAmp — AudioEngine
 *
 * Purpose:
 * Future orchestration layer for DSP graph management,
 * device selection, and session lifecycle control.
 *
 * Real-time safety:
 * Audio thread must remain lock-free, allocation-free,
 * and free of blocking I/O operations.
 *
 * Threading model:
 * Not yet implemented. Planned separation between:
 * - Control thread (UI / CLI / MIDI)
 * - Audio callback thread (RT-safe DSP)
 *
 * Limitations:
 * This is currently a stub. No DSP graph is wired yet.
 * Serves as architectural placeholder for upcoming milestones.
 */

#pragma once

namespace RenaAmp {

/**
 * @brief Core engine responsible for audio session lifecycle.
 *
 * @note Stub implementation.
 *       Will eventually manage DSP graph execution,
 *       device routing, and real-time processing state.
 */
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    /**
     * @brief Initialize audio system and prepare DSP graph.
     *
     * @return true if initialization succeeds.
     *
     * @pre None.
     * @post Engine is ready for real-time processing.
     *
     * @warning Must not be called from audio callback thread.
     */
    bool initialize();

    /**
     * @brief Shutdown audio system safely.
     *
     * @pre Engine was initialized.
     * @post All audio resources released.
     */
    void shutdown();

    /**
     * @brief Check runtime state.
     */
    bool isRunning() const;

private:
    bool initialized_ = false;

    /**
     * @note Control-thread owned state only.
     *       Never accessed from audio callback thread.
     */
};

} // namespace RenaAmp
