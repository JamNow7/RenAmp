/*
 * Renamp — NAMProcessor
 * Purpose: host Neural Amp Modeler inference and wet/dry mixing.
 * Real-time safety: no allocations/locks/I/O in process(); models load off the audio thread.
 * Threading: control thread loads/selects models and updates params; audio thread runs process().
 * Processing: typically before Cabinet IR; see DSP chain order.
 */
#include "dsp/nam_processor.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#include <NAM/dsp.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace RenaAmp {

#ifdef __ARM_NEON
inline void neon_mix_wet_dry(float* __restrict left, float* __restrict right,
                             const float* __restrict dry_left, const float* __restrict dry_right,
                             const float* __restrict wet_left, const float* __restrict wet_right,
                             size_t count, float mix_dry, float wet_gain) {
    size_t i = 0;
    const float32x4_t dry_gain_vec = vdupq_n_f32(mix_dry);
    const float32x4_t wet_gain_vec = vdupq_n_f32(wet_gain);

    for (; i + 4 <= count; i += 4) {
        float32x4_t dl = vld1q_f32(dry_left + i);
        float32x4_t dr = vld1q_f32(dry_right + i);
        float32x4_t wl = vld1q_f32(wet_left + i);
        float32x4_t wr = vld1q_f32(wet_right + i);
        float32x4_t out_l = vmlaq_f32(vmulq_f32(dl, dry_gain_vec), wl, wet_gain_vec);
        float32x4_t out_r = vmlaq_f32(vmulq_f32(dr, dry_gain_vec), wr, wet_gain_vec);
        vst1q_f32(left + i, out_l);
        vst1q_f32(right + i, out_r);
    }

    for (; i < count; ++i) {
        left[i] = dry_left[i] * mix_dry + wet_left[i] * wet_gain;
        right[i] = dry_right[i] * mix_dry + wet_right[i] * wet_gain;
    }
}
#endif

NAMProcessor::NAMProcessor() {
    slots_[0] = std::make_unique<ModelSlot>();
    slots_[1] = std::make_unique<ModelSlot>();
}

NAMProcessor::~NAMProcessor() = default;

void NAMProcessor::initialize(float sample_rate) {
    sample_rate_ = sample_rate;
    std::cout << "NAMProcessor::initialize sample_rate_ = " << sample_rate_ << std::endl;

    bypass_.store(true, std::memory_order_relaxed);
    active_slot_.store(0, std::memory_order_relaxed);

    input_gain_smoother_.init(sample_rate, 0.0f);
    output_gain_smoother_.init(sample_rate, 0.0f);
    mix_smoother_.init(sample_rate, 1.0f);

    wavenet_input_buffer_.resize(MAX_BUFFER_SIZE);
    wavenet_output_buffer_.resize(MAX_BUFFER_SIZE);
    temp_dry_left_.resize(MAX_BUFFER_SIZE);
    temp_dry_right_.resize(MAX_BUFFER_SIZE);
    temp_wet_left_.resize(MAX_BUFFER_SIZE);
    temp_wet_right_.resize(MAX_BUFFER_SIZE);

    for (int i = 0; i < 2; ++i) {
        slots_[i]->model = nullptr;
        slots_[i]->weights = nullptr;
        slots_[i]->weights_offset = 0;
        slots_[i]->lstm_state_h.clear();
        slots_[i]->lstm_state_c.clear();
        slots_[i]->num_layers = 0;
        slots_[i]->hidden_size = 0;
        slots_[i]->use_wavenet = false;
        slots_[i]->nam_dsp.reset();
        slots_[i]->dc_block_x_prev = 0.0f;
        slots_[i]->dc_block_y_prev = 0.0f;
        slots_[i]->ready.store(false, std::memory_order_relaxed);
        slots_[i]->active.store(false, std::memory_order_relaxed);
    }
}

void NAMProcessor::process(float* left, float* right, size_t count) {
    if (bypass_.load(std::memory_order_relaxed)) {
        return;
    }

    int slot_idx = active_slot_.load(std::memory_order_relaxed);
    auto& active_slot = slots_[slot_idx];

    if (!active_slot->active.load(std::memory_order_relaxed)) {
        return;
    }

    const NAMModel* model = active_slot->model;
    if (!model || !model->isReady()) {
        return;
    }

    if (count > MAX_BUFFER_SIZE) {
        return;
    }

    const float input_gain_db = input_gain_smoother_.next();
    const float output_gain_db = output_gain_smoother_.next();
    const float mix = mix_smoother_.next();

    const float input_gain = dbToLinear(input_gain_db);
    const float output_gain = dbToLinear(output_gain_db);
    const float mix_dry = 1.0f - mix;
    const float mix_wet = mix;

    // DEBUG: Remove this later
    static int debug_count = 0;
    if (debug_count < 3) {
        std::cout << "DEBUG: mix=" << mix << " mix_dry=" << mix_dry << " mix_wet=" << mix_wet << std::endl;
        debug_count++;
    }

    if (active_slot->use_wavenet && active_slot->nam_dsp) {
        std::copy(left, left + count, temp_dry_left_.data());
        std::copy(right, right + count, temp_dry_right_.data());

        // Tiny noise guard to avoid denormals in deep silence
        static uint32_t tcounter = 0;
        const float tiny = 1e-20f;
        for (size_t i = 0; i < count; ++i) {
            float guard = (tcounter++ & 1) ? tiny : -tiny;
            wavenet_input_buffer_[i] = static_cast<double>((left[i] * input_gain) + guard);
        }

        active_slot->nam_dsp->process(
            wavenet_input_buffer_.data(),
            wavenet_output_buffer_.data(),
            static_cast<int>(count)
        );

        for (size_t i = 0; i < count; ++i) {
            temp_wet_left_[i] = static_cast<float>(wavenet_output_buffer_[i]);
            temp_wet_right_[i] = temp_wet_left_[i];
        }

#ifdef __ARM_NEON
        neon_mix_wet_dry(left, right,
                         temp_dry_left_.data(), temp_dry_right_.data(),
                         temp_wet_left_.data(), temp_wet_right_.data(),
                         count, mix_dry, mix_wet * output_gain);
#else
        for (size_t i = 0; i < count; ++i) {
            float wet = temp_wet_left_[i];
            left[i] = temp_dry_left_[i] * mix_dry + wet * mix_wet * output_gain;
            right[i] = temp_dry_right_[i] * mix_dry + wet * mix_wet * output_gain;
        }
#endif
    } else {
        for (size_t i = 0; i < count; ++i) {
            float dry = left[i];
            // Tiny noise guard to avoid denormals in deep silence
            static uint32_t lcounter = 0;
            const float ltiny = 1e-20f;
            float x = dry * input_gain + ((lcounter++ & 1) ? ltiny : -ltiny);
            float wet = lstmForwardPass(x, *active_slot);
            left[i] = dry * mix_dry + wet * mix_wet * output_gain;
            right[i] = left[i];
        }
    }
}

bool NAMProcessor::setModel(const NAMModel* model, int buffer_size) {
    if (!model || !model->isReady()) {
        std::cerr << "Invalid model provided to NAMProcessor" << std::endl;
        return false;
    }

    if (buffer_size <= 0 || buffer_size > static_cast<int>(MAX_BUFFER_SIZE)) {
        std::cerr << "Invalid buffer size for NAMProcessor: " << buffer_size << std::endl;
        return false;
    }

    const NAMConfig& config = model->getConfig();

    int active = active_slot_.load(std::memory_order_relaxed);
    int inactive = 1 - active;
    auto& slot = slots_[inactive];

    slot->model = model;
    slot->use_wavenet = (config.architecture == NAMArchitecture::kWaveNet);
    slot->weights = nullptr;
    slot->weights_offset = 0;
    slot->num_layers = 0;
    slot->hidden_size = 0;
    slot->dc_block_x_prev = 0.0f;
    slot->dc_block_y_prev = 0.0f;
    slot->ready.store(false, std::memory_order_relaxed);
    slot->active.store(false, std::memory_order_relaxed);

    if (slot->use_wavenet) {
        std::cout << "NAMProcessor: Loading WaveNet model using NeuralAmpModelerCore..." << std::endl;
        std::cout << "NAMProcessor: model path = " << model->getFilePath() << std::endl;
        std::cout << "NAMProcessor: sample_rate_ before Reset = " << sample_rate_ << std::endl;
        std::cout << "NAMProcessor: buffer_size before Reset = " << buffer_size << std::endl;

        slot->nam_dsp = nam::get_dsp(model->getFilePath().c_str());
        std::cout << "NAMProcessor: get_dsp returned " << (slot->nam_dsp ? "valid" : "nullptr") << std::endl;

        if (!slot->nam_dsp) {
            std::cerr << "Failed to create WaveNet DSP from file" << std::endl;
            return false;
        }

        slot->nam_dsp->Reset(static_cast<double>(sample_rate_), buffer_size);
        std::cout << "NAMProcessor: DSP reset OK" << std::endl;
        std::cout << "NAMProcessor: WaveNet model loaded (buffer_size=" << buffer_size
                  << ", sample_rate=" << sample_rate_ << ")" << std::endl;
    } else {
        slot->nam_dsp.reset();
        slot->weights = model->getWeights();
        slot->num_layers = config.num_layers;
        slot->hidden_size = config.hidden_size;

        size_t state_size = static_cast<size_t>(config.num_layers * config.hidden_size);
        slot->lstm_state_h.assign(state_size, 0.0f);
        slot->lstm_state_c.assign(state_size, 0.0f);

        const int H = config.hidden_size;
        size_t offset = 0;
        for (int layer = 0; layer < config.num_layers; ++layer) {
            const int input_size = (layer == 0) ? config.input_size : H;
            size_t W_size = 4 * H * (input_size + H);
            size_t b_size = 4 * H;
            offset += W_size + b_size;
        }
        slot->weights_offset = offset;

        std::cout << "NAMProcessor: LSTM model loaded ("
                  << config.num_layers << " layers, "
                  << config.hidden_size << " hidden size)" << std::endl;
    }

    slot->ready.store(true, std::memory_order_release);
    slot->active.store(true, std::memory_order_release);
    slots_[active]->active.store(false, std::memory_order_release);
    active_slot_.store(inactive, std::memory_order_release);
    bypass_.store(false, std::memory_order_relaxed);
    return true;
}

void NAMProcessor::setBypass(bool bypass) {
    bypass_.store(bypass, std::memory_order_relaxed);
}

void NAMProcessor::setInputGain(float gain_db, float smooth_time) {
    input_gain_smoother_.setTarget(std::clamp(gain_db, -20.0f, 20.0f),
                                   smooth_time > 0 ? smooth_time : 0.01f);
}

void NAMProcessor::setOutputGain(float gain_db, float smooth_time) {
    output_gain_smoother_.setTarget(std::clamp(gain_db, -20.0f, 20.0f),
                                    smooth_time > 0 ? smooth_time : 0.01f);
}

void NAMProcessor::setMix(float mix, float smooth_time) {
    float clamped_mix = std::clamp(mix, 0.0f, 1.0f);
    // Use 0 duration for immediate change, otherwise use provided time or default 50ms
    float duration = (smooth_time == 0.0f) ? 0.0f : (smooth_time > 0 ? smooth_time : 0.05f);
    mix_smoother_.setTarget(clamped_mix, duration);
}

bool NAMProcessor::isModelLoaded() const {
    int slot = active_slot_.load(std::memory_order_relaxed);
    return slots_[slot]->active.load(std::memory_order_relaxed);
}

const NAMMetadata* NAMProcessor::getModelInfo() const {
    int slot = active_slot_.load(std::memory_order_relaxed);
    const NAMModel* model = slots_[slot]->model;
    if (model && model->isReady()) {
        return &model->getMetadata();
    }
    return nullptr;
}

void NAMProcessor::processLSTM(float* left, float* right, size_t count, ModelSlot& slot) {
    for (size_t i = 0; i < count; ++i) {
        float out = lstmForwardPass(left[i], slot);
        left[i] = out;
        right[i] = out;
    }
}

float NAMProcessor::lstmForwardPass(float x, ModelSlot& slot) {
    // ========================================================================
    // LSTM FORWARD PASS: Single-sample inference through all layers
    // ========================================================================
    // Per-layer structure: input → [i, f, g, o gates] → cell state → hidden state.
    // Uses fastSigmoid/fastTanh approximations instead of std:: versions for RT performance.
    // Weight layout: [W_i | W_f | W_g | W_o] concatenated, followed by biases [b_i | b_f | b_g | b_o].
    // ========================================================================

    const int L = slot.num_layers;
    const int H = slot.hidden_size;
    const float* w = slot.weights;
    float* h = slot.lstm_state_h.data();
    float* c = slot.lstm_state_c.data();
    size_t weight_offset = 0;

    for (int layer = 0; layer < L; ++layer) {
        const int I = (layer == 0) ? 1 : H;
        float* h_layer = h + layer * H;
        float* c_layer = c + layer * H;
        const float* W = w + weight_offset;
        const float* b = W + (4 * H * (I + H));

        for (int j = 0; j < H; ++j) {
            float i_sum = b[j];
            float f_sum = b[j + H];
            float g_sum = b[j + 2*H];
            float o_sum = b[j + 3*H];

            float input_val = (layer == 0) ? x : h_layer[j];

            i_sum += W[j * (I + H) + 0] * input_val;
            f_sum += W[(H + j) * (I + H) + 0] * input_val;
            g_sum += W[(2*H + j) * (I + H) + 0] * input_val;
            o_sum += W[(3*H + j) * (I + H) + 0] * input_val;

            const float* h_recurrence = h_layer;
            for (int k = 0; k < H; ++k) {
                int col = 1 + k;
                i_sum += W[j * (I + H) + col] * h_recurrence[k];
                f_sum += W[(H + j) * (I + H) + col] * h_recurrence[k];
                g_sum += W[(2*H + j) * (I + H) + col] * h_recurrence[k];
                o_sum += W[(3*H + j) * (I + H) + col] * h_recurrence[k];
            }

            float i_gate = fastSigmoid(i_sum);
            float f_gate = fastSigmoid(f_sum);
            float g_gate = fastTanh(g_sum);
            float o_gate = fastSigmoid(o_sum);

            c_layer[j] = f_gate * c_layer[j] + i_gate * g_gate;
            h_layer[j] = o_gate * fastTanh(c_layer[j]);
        }

        weight_offset += 4 * H * (I + H) + 4 * H + 2 * H;
    }

    // ========================================================================
    // DENSE OUTPUT LAYER: Project last hidden state to output
    // ========================================================================
    const float* head_weight = w + slot.weights_offset;
    const float head_bias = head_weight[H];
    const float* h_last = h + (L - 1) * H;

    float y = head_bias;
    for (int j = 0; j < H; ++j) {
        y += head_weight[j] * h_last[j];
    }

    // DC blocking filter (highpass ~5 Hz) to remove neural model DC offset.
    // y[n] = x[n] - x[n-1] + R * y[n-1]; R = 0.995 for ~5 Hz cutoff at 48 kHz.
    const float R = 0.995f;
    float dc_blocked = y - slot.dc_block_x_prev + R * slot.dc_block_y_prev;
    slot.dc_block_x_prev = y;
    slot.dc_block_y_prev = dc_blocked;

    return dc_blocked;
}

float NAMProcessor::fastTanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float NAMProcessor::fastSigmoid(float x) {
    return 0.5f + 0.5f * fastTanh(x * 0.5f);
}

float NAMProcessor::dbToLinear(float db) const {
    return std::exp(0.1151292546497022842f * db);
}

} // namespace RenaAmp