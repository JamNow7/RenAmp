/*
 * Renamp — CabinetProcessor (IR convolution)
 * Purpose: load and convolve cabinet impulse responses; handle bypass/mix and slot swapping.
 * Real-time safety: no allocations, locks, or I/O in process(); IRs are preloaded/normalized and swapped atomically.
 * Threading: control thread loads/normalizes IR; audio thread calls process(); swap via double-buffer.
 */
#include "dsp/cabinet_processor.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <iostream>

// ARM NEON SIMD intrinsics for Apple Silicon optimization
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace RenaAmp {

// WAV file header structure
#pragma pack(push, 1)
struct WAVHeader {
    char riff[4];           // "RIFF"
    uint32_t file_size;     // Total file size - 8
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmt_size;      // Format chunk size (16 for PCM)
    uint16_t audio_format;  // 1 = PCM
    uint16_t num_channels;  // 1 = mono, 2 = stereo
    uint32_t sample_rate;   // Sample rate
    uint32_t byte_rate;     // byte_rate = sample_rate * num_channels * bits_per_sample/8
    uint16_t block_align;   // block_align = num_channels * bits_per_sample/8
    uint16_t bits_per_sample;
    char data[4];           // "data"
    uint32_t data_size;     // Data size in bytes
};
#pragma pack(pop)

CabinetProcessor::CabinetProcessor() {
    // Create two slots for double buffering
    slots_[0] = std::make_unique<IRSlot>();
    slots_[1] = std::make_unique<IRSlot>();
}

CabinetProcessor::~CabinetProcessor() = default;

void CabinetProcessor::initialize(float sample_rate) {
    sample_rate_ = sample_rate;

    // Reset state
    bypass_.store(true, std::memory_order_relaxed);
    active_slot_.store(0, std::memory_order_relaxed);

    // Initialize mix smoother
    mix_smoother_.init(sample_rate, 1.0f);

    // Pre-reserve temporary buffers to avoid RT reallocations
    // Reservamos bastante margen para bloques pequeños (JACK 64/128) y picos eventuales
    const size_t kTempReserve = 8192; // 8k samples por canal (~170 ms a 48 kHz)
    temp_dry_left_.reserve(kTempReserve);
    temp_dry_right_.reserve(kTempReserve);

    // Initialize slots
    for (int i = 0; i < 2; ++i) {
        slots_[i]->ir_left.clear();
        slots_[i]->ir_right.clear();
        slots_[i]->history_left.assign(max_ir_length_, 0.0f);
        slots_[i]->history_right.assign(max_ir_length_, 0.0f);
        slots_[i]->write_pos_left = 0;
        slots_[i]->write_pos_right = 0;
        slots_[i]->ready.store(false, std::memory_order_relaxed);
        slots_[i]->active.store(false, std::memory_order_relaxed);
    }
}

void CabinetProcessor::process(float* left, float* right, size_t count) {
    // Check bypass
    if (bypass_.load(std::memory_order_relaxed)) {
        return;  // Passthrough
    }

    // Get active slot
    int slot = active_slot_.load(std::memory_order_relaxed);
    auto& active_slot = slots_[slot];

    // Check if IR is loaded
    if (!active_slot->active.load(std::memory_order_relaxed)) {
        return;  // No IR loaded, passthrough
    }

    // Get smoothed mix parameter (per-sample for smooth transitions)
    // For efficiency, we process the entire block with one interpolated value
    // This is a compromise between per-sample accuracy and performance
    float mix = mix_smoother_.next();  // Use first sample value for entire block
    const float mix_dry = 1.0f - mix;
    const float mix_wet = mix;

    // Save dry signal before convolution overwrites the buffers
    // Use temp_dry buffers since processChannel operates in-place
    temp_dry_left_.resize(count);
    temp_dry_right_.resize(count);
    std::copy(left, left + count, temp_dry_left_.data());
    std::copy(right, right + count, temp_dry_right_.data());

    // Process each channel (in-place convolution)
    processChannel(left, left, count,
                   active_slot->ir_left.data(),
                   active_slot->ir_left.size(),
                   active_slot->history_left.data(),
                   active_slot->write_pos_left);

    processChannel(right, right, count,
                   active_slot->ir_right.data(),
                   active_slot->ir_right.size(),
                   active_slot->history_right.data(),
                   active_slot->write_pos_right);

    // Apply dry/wet mix: output = dry * (1-mix) + wet * mix
    for (size_t i = 0; i < count; ++i) {
        left[i] = temp_dry_left_[i] * mix_dry + left[i] * mix_wet;
        right[i] = temp_dry_right_[i] * mix_dry + right[i] * mix_wet;
    }
}

bool CabinetProcessor::loadIR(const std::string& filepath) {
    // Find inactive slot
    int active = active_slot_.load(std::memory_order_relaxed);
    int inactive = 1 - active;
    auto& slot = slots_[inactive];

    // Load WAV file
    bool success = loadWAV(filepath, slot->ir_left, slot->ir_right);

    if (success) {
        // Normalize IR to prevent clipping
        normalizeIR(slot->ir_left);
        normalizeIR(slot->ir_right);

        // Reset history buffer and write position
        std::fill(slot->history_left.begin(), slot->history_left.end(), 0.0f);
        std::fill(slot->history_right.begin(), slot->history_right.end(), 0.0f);
        slot->write_pos_left = 0;
        slot->write_pos_right = 0;

        // Mark as ready
        slot->ready.store(true, std::memory_order_release);

        // Swap active slot
        slot->active.store(true, std::memory_order_release);
        slots_[active]->active.store(false, std::memory_order_release);
        active_slot_.store(inactive, std::memory_order_release);

        // Disable bypass since we now have an IR
        bypass_.store(false, std::memory_order_relaxed);
    }

    return success;
}

void CabinetProcessor::setBypass(bool bypass) {
    bypass_.store(bypass, std::memory_order_relaxed);
}

void CabinetProcessor::setMix(float mix, float smooth_time) {
    mix_smoother_.setTarget(std::clamp(mix, 0.0f, 1.0f), smooth_time > 0 ? smooth_time : 0.05f);
}

bool CabinetProcessor::isIRLoaded() const {
    int slot = active_slot_.load(std::memory_order_relaxed);
    return slots_[slot]->active.load(std::memory_order_relaxed);
}

size_t CabinetProcessor::getIRLength() const {
    int slot = active_slot_.load(std::memory_order_relaxed);
    return slots_[slot]->ir_left.size();
}

bool CabinetProcessor::loadWAV(const std::string& filepath,
                                std::vector<float>& left,
                                std::vector<float>& right) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open IR file: " << filepath << std::endl;
        return false;
    }

    // Read and validate RIFF header
    char riff[4];
    uint32_t file_size;
    char wave[4];

    file.read(riff, 4);
    file.read(reinterpret_cast<char*>(&file_size), 4);
    file.read(wave, 4);

    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0) {
        std::cerr << "Invalid WAV file format (no RIFF/WAVE)" << std::endl;
        return false;
    }

    // Skip chunks until we find "fmt "
    char chunk_id[4];
    uint32_t chunk_size;
    bool found_fmt = false;
    bool found_data = false;

    while (!found_fmt && !file.eof()) {
        file.read(chunk_id, 4);
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            found_fmt = true;
            break;
        } else {
            // Skip this chunk (JUNK or other)
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!found_fmt) {
        std::cerr << "fmt chunk not found" << std::endl;
        return false;
    }

    // Read fmt data
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    file.read(reinterpret_cast<char*>(&audio_format), 2);
    file.read(reinterpret_cast<char*>(&num_channels), 2);
    file.read(reinterpret_cast<char*>(&sample_rate), 4);
    file.read(reinterpret_cast<char*>(&byte_rate), 4);
    file.read(reinterpret_cast<char*>(&block_align), 2);
    file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

    // Skip any extra bytes in fmt chunk
    if (chunk_size > 16) {
        file.seekg(chunk_size - 16, std::ios::cur);
    }

    // Check for PCM format
    if (audio_format != 1) {
        std::cerr << "Only PCM WAV files are supported (format: " << audio_format << ")" << std::endl;
        return false;
    }

    // Check bit depth (support 16, 24, and 32 bit)
    if (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32) {
        std::cerr << "Unsupported bit depth: " << bits_per_sample << " (only 16/24/32-bit supported)" << std::endl;
        return false;
    }

    // Check number of channels
    if (num_channels != 1 && num_channels != 2) {
        std::cerr << "Only mono and stereo WAV files are supported (channels: " << num_channels << ")" << std::endl;
        return false;
    }

    // Skip chunks until we find "data"
    uint32_t data_size = 0;
    while (!found_data && !file.eof()) {
        file.read(chunk_id, 4);
        file.read(reinterpret_cast<char*>(&chunk_size), 4);

        if (std::strncmp(chunk_id, "data", 4) == 0) {
            found_data = true;
            data_size = chunk_size;
            break;
        } else {
            // Skip this chunk
            file.seekg(chunk_size, std::ios::cur);
        }
    }

    if (!found_data) {
        std::cerr << "data chunk not found" << std::endl;
        return false;
    }

    // Calculate number of samples
    uint32_t num_samples = data_size / (bits_per_sample / 8) / num_channels;

    // Limit IR length
    if (num_samples > max_ir_length_) {
        std::cout << "IR is too long (" << num_samples << " samples), truncating to "
                  << max_ir_length_ << " samples" << std::endl;
        num_samples = max_ir_length_;
    }

    std::cout << "Loading IR: " << num_samples << " samples, "
              << num_channels << " channels, "
              << sample_rate << " Hz, " << bits_per_sample << "-bit" << std::endl;

    // Read sample data based on bit depth
    std::vector<uint8_t> raw_data;

    if (bits_per_sample == 16) {
        // 16-bit: 2 bytes per sample
        raw_data.resize(num_samples * num_channels * 2);
        file.read(reinterpret_cast<char*>(raw_data.data()), raw_data.size());
    } else if (bits_per_sample == 24) {
        // 24-bit: 3 bytes per sample
        raw_data.resize(num_samples * num_channels * 3);
        file.read(reinterpret_cast<char*>(raw_data.data()), raw_data.size());
    } else if (bits_per_sample == 32) {
        // 32-bit: 4 bytes per sample
        raw_data.resize(num_samples * num_channels * 4);
        file.read(reinterpret_cast<char*>(raw_data.data()), raw_data.size());
    }

    // Convert to float and split channels
    left.resize(num_samples);
    right.resize(num_samples);

    if (bits_per_sample == 16) {
        int16_t* data_16 = reinterpret_cast<int16_t*>(raw_data.data());

        if (num_channels == 1) {
            // Mono: copy to both channels
            for (size_t i = 0; i < num_samples; ++i) {
                float sample = static_cast<float>(data_16[i]) / 32768.0f;
                left[i] = sample;
                right[i] = sample;
            }
        } else {
            // Stereo: interleave
            for (size_t i = 0; i < num_samples; ++i) {
                left[i] = static_cast<float>(data_16[i * 2]) / 32768.0f;
                right[i] = static_cast<float>(data_16[i * 2 + 1]) / 32768.0f;
            }
        }
    } else if (bits_per_sample == 24) {
        // 24-bit: little-endian, 3 bytes per sample
        if (num_channels == 1) {
            // Mono: copy to both channels
            for (size_t i = 0; i < num_samples; ++i) {
                int32_t sample = (raw_data[i * 3] | (raw_data[i * 3 + 1] << 8) | (raw_data[i * 3 + 2] << 16));
                // Sign extend from 24-bit to 32-bit
                if (sample & 0x800000) sample |= 0xFF000000;
                float sample_float = static_cast<float>(sample) / 8388608.0f;  // 2^23
                left[i] = sample_float;
                right[i] = sample_float;
            }
        } else {
            // Stereo: interleave
            for (size_t i = 0; i < num_samples; ++i) {
                int32_t sample_l = (raw_data[i * 6] | (raw_data[i * 6 + 1] << 8) | (raw_data[i * 6 + 2] << 16));
                int32_t sample_r = (raw_data[i * 6 + 3] | (raw_data[i * 6 + 4] << 8) | (raw_data[i * 6 + 5] << 16));
                // Sign extend
                if (sample_l & 0x800000) sample_l |= 0xFF000000;
                if (sample_r & 0x800000) sample_r |= 0xFF000000;
                left[i] = static_cast<float>(sample_l) / 8388608.0f;
                right[i] = static_cast<float>(sample_r) / 8388608.0f;
            }
        }
    } else if (bits_per_sample == 32) {
        int32_t* data_32 = reinterpret_cast<int32_t*>(raw_data.data());

        if (num_channels == 1) {
            // Mono: copy to both channels
            for (size_t i = 0; i < num_samples; ++i) {
                float sample = static_cast<float>(data_32[i]) / 2147483648.0f;  // 2^31
                left[i] = sample;
                right[i] = sample;
            }
        } else {
            // Stereo: interleave
            for (size_t i = 0; i < num_samples; ++i) {
                left[i] = static_cast<float>(data_32[i * 2]) / 2147483648.0f;
                right[i] = static_cast<float>(data_32[i * 2 + 1]) / 2147483648.0f;
            }
        }
    }

    return true;
}

void CabinetProcessor::processChannel(float* output, const float* input, size_t count,
                                      const float* ir, size_t ir_length, float* history, size_t& write_pos) {
    // ========================================================================
    // REAL-TIME CONVOLUTION: Circular buffer with ARM NEON SIMD optimization
    // ========================================================================
    // Design: No buffer shifting (expensive). Use circular write position.
    //          Pre-fetch to L1 cache for latency hiding. NEON for 4× MAC parallelism.
    // ========================================================================

    constexpr size_t PREFETCH_DISTANCE = 4;  // Pre-fetch 4 samples ahead (empirical: hides L1 latency)

    for (size_t i = 0; i < count; ++i) {
        // Write new sample and advance circular position
        history[write_pos] = input[i];

        // Convolve: sum = Σ(history[k] × ir[j]) over IR length
        float sum = 0.0f;

#ifdef __ARM_NEON
        // ------------------------------------------------------------------------
        // ARM NEON SIMD PATH: 4 MACs per iteration
        // ------------------------------------------------------------------------
        size_t j = 0;
        const size_t limit = ir_length & ~3u;  // Round down to multiple of 4 for SIMD

        float32x4_t sum_vec = vdupq_n_f32(0.0f);

        // Pre-fetch first history sample to L1 cache (read, temporal locality)
        size_t history_idx = (write_pos > 0) ? (write_pos - 1) : (ir_length - 1);
        __builtin_prefetch(&history[history_idx], 0, 1);

        // SIMD loop: 4 IR coefficients × 4 history samples per iteration
        for (; j < limit; j += 4) {
            float32x4_t ir_vec = vld1q_f32(&ir[j]);  // Sequential: cache-friendly

            // Circular indexing: wrap around when (write_pos - 1 - j) < 0
            float h0 = history[(write_pos > j) ? (write_pos - 1 - j) : (ir_length + write_pos - 1 - j)];
            float h1 = history[(write_pos > j + 1) ? (write_pos - 2 - j) : (ir_length + write_pos - 2 - j)];
            float h2 = history[(write_pos > j + 2) ? (write_pos - 3 - j) : (ir_length + write_pos - 3 - j)];
            float h3 = history[(write_pos > j + 3) ? (write_pos - 4 - j) : (ir_length + write_pos - 4 - j)];

            float32x4_t hist_vec = {h0, h1, h2, h3};
            sum_vec = vmlaq_f32(sum_vec, hist_vec, ir_vec);  // Fused multiply-add

            // Pre-fetch next history block
            if (j + 8 < ir_length) {
                history_idx = (write_pos > j + 4) ? (write_pos - 5 - j) : (ir_length + write_pos - 5 - j);
                __builtin_prefetch(&history[history_idx], 0, 1);
            }
        }

        sum = vaddvq_f32(sum_vec);  // Horizontal sum: [s0,s1,s2,s3] → s0+s1+s2+s3

        // Scalar tail: process remaining coefficients (ir_length % 4)
        for (; j < ir_length; ++j) {
            size_t hist_idx = (write_pos > j) ? (write_pos - 1 - j) : (ir_length + write_pos - 1 - j);
            sum += history[hist_idx] * ir[j];
        }

#else
        // ------------------------------------------------------------------------
        // SCALAR PATH: Manual unrolling for ILP (instruction-level parallelism)
        // ------------------------------------------------------------------------
        size_t history_idx;

        history_idx = (write_pos > 0) ? (write_pos - 1) : (ir_length - 1);
        __builtin_prefetch(&history[history_idx], 0, 1);

        size_t j = 0;
        const size_t limit = ir_length & ~3u;  // Round down to multiple of 4

        // Unrolled: 4 MACs per iteration (compiler fuses to FMA)
        for (; j < limit; j += 4) {
            history_idx = (write_pos > j) ? (write_pos - 1 - j) : (ir_length + write_pos - 1 - j);
            float h0 = history[history_idx];

            history_idx = (write_pos > j + 1) ? (write_pos - 2 - j) : (ir_length + write_pos - 2 - j);
            float h1 = history[history_idx];

            history_idx = (write_pos > j + 2) ? (write_pos - 3 - j) : (ir_length + write_pos - 3 - j);
            float h2 = history[history_idx];

            history_idx = (write_pos > j + 3) ? (write_pos - 4 - j) : (ir_length + write_pos - 4 - j);
            float h3 = history[history_idx];

            sum += h0 * ir[j];
            sum += h1 * ir[j + 1];
            sum += h2 * ir[j + 2];
            sum += h3 * ir[j + 3];
        }

        // Tail: remaining coefficients
        for (; j < ir_length; ++j) {
            history_idx = (write_pos > j) ? (write_pos - 1 - j) : (ir_length + write_pos - 1 - j);
            sum += history[history_idx] * ir[j];
        }

#endif // __ARM_NEON

        output[i] = sum;
        write_pos = (write_pos + 1) % ir_length;  // Circular advance

        // Pre-fetch next input sample
        if (i + PREFETCH_DISTANCE < count) {
            __builtin_prefetch(&input[i + PREFETCH_DISTANCE], 0, 1);
        }
    }
}

void CabinetProcessor::normalizeIR(std::vector<float>& ir) {
    // Find peak
    float peak = 0.0f;
    for (float sample : ir) {
        peak = std::max(peak, std::fabs(sample));
    }

    if (peak > 0.0001f) {
        // Normalize to -6 dBFS (0.5 linear) to preserve cabinet tonal balance
        // Full 0 dBFS normalization destroys the spectral shape of the IR
        float target_peak = 0.5f;
        float gain = target_peak / peak;
        for (float& sample : ir) {
            sample *= gain;
        }
    }
}

void CabinetProcessor::swapIRSlot() {
    // Swap active slot at block boundary
    int current = active_slot_.load(std::memory_order_relaxed);
    int next = 1 - current;

    if (slots_[next]->ready.load(std::memory_order_acquire)) {
        slots_[current]->active.store(false, std::memory_order_release);
        slots_[next]->active.store(true, std::memory_order_release);
        active_slot_.store(next, std::memory_order_release);
    }
}

} // namespace RenaAmp
