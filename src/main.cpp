/*
 * Renamp — JACK entry points and lifecycle
 * Purpose: initialize JACK, wire ports, run DSP callbacks, and handle shutdown.
 * Real-time safety: audio callbacks must avoid allocations, locks, and I/O/logging; limit work to fast-path DSP and routing.
 * Threading: JACK audio thread invokes jack_process_callback(); main/control thread handles setup, params, and signals.
 * Processing order: NAM → Cabinet → Master Gain → Limiter (see dsp_chain.cpp).
 * Note: auto-connect is best-effort; users may need to select ports manually.
 */
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <filesystem>
#include <system_error>

#include <jack/jack.h>

#include "dsp/dsp_chain.h"
#include "models/nam_model.h"

// =========================
// Global state
// =========================
std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_xrun_count{0};

RenaAmp::DSPChain g_dsp_chain;
RenaAmp::NAMModel g_nam_model;

// Test flags
bool g_bypass_all = false;
bool g_connect_ports = true;

// JACK client and ports
jack_client_t* g_jack_client = nullptr;
jack_port_t* g_input_port_left = nullptr;
jack_port_t* g_input_port_right = nullptr;
jack_port_t* g_output_port_left = nullptr;
jack_port_t* g_output_port_right = nullptr;

// =========================
// JACK callbacks
// =========================

int jack_process_callback(jack_nframes_t nframes, void* /*arg*/) {
    // Get JACK buffers
    auto* in_left = static_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_input_port_left, nframes));
    auto* in_right = static_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_input_port_right, nframes));
    auto* out_left = static_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_output_port_left, nframes));
    auto* out_right = static_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_output_port_right, nframes));

    if (!in_left || !out_left || !out_right) {
        return 0; // Nothing to do
    }

    // Fallback to mono if right input is not connected
    if (!in_right) {
        in_right = in_left;
    }

    const size_t bytes = static_cast<size_t>(nframes) * sizeof(jack_default_audio_sample_t);

    // Copy input to output buffers (in-place processing on outputs)
    std::memcpy(out_left,  in_left,  bytes);
    std::memcpy(out_right, in_right, bytes);

    // Process stereo buffers only if not bypassed
    if (!g_bypass_all) {
        g_dsp_chain.process(out_left, out_right, nframes);
    }

    return 0;
}

int jack_xrun_callback(void* /*arg*/) {
    g_xrun_count.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

void jack_shutdown_callback(void* /*arg*/) {
    std::cerr << "\nJACK server shut down. Exiting..." << std::endl;
    g_running.store(false);
}

// =========================
// Helpers
// =========================

std::string absolute_or_keep(const char* path) {
    if (path == nullptr) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path), ec);

    if (ec) {
        return std::string(path);
    }

    return abs.string();
}

void print_ports() {
    std::cout << "\n=== Available JACK Ports ===" << std::endl;

    const char** all_ports = jack_get_ports(g_jack_client, nullptr, nullptr, 0);
    if (all_ports != nullptr) {
        for (int i = 0; all_ports[i] != nullptr; ++i) {
            jack_port_t* p = jack_port_by_name(g_jack_client, all_ports[i]);
            if (!p) continue;

            int flags = jack_port_flags(p);
            const char* type = (flags & JackPortIsInput) ? "INPUT" : "OUTPUT";
            std::cout << "  [" << type << "] " << all_ports[i] << std::endl;
        }
        jack_free(all_ports);
    }

    std::cout << "=============================\n" << std::endl;
}

void auto_connect_ports() {
    const char* client_name = jack_get_client_name(g_jack_client);
    if (!client_name) return;

    const char** out_ports = jack_get_ports(g_jack_client, nullptr, nullptr, JackPortIsOutput);
    if (out_ports != nullptr) {
        std::cout << "Connecting input ports..." << std::endl;
        int connected = 0;

        for (int i = 0; out_ports[i] != nullptr && connected < 2; ++i) {
            if (std::strstr(out_ports[i], client_name) != nullptr) continue;

            const char* dst =
                (connected == 0) ? jack_port_name(g_input_port_left)
                                 : jack_port_name(g_input_port_right);

            if (jack_connect(g_jack_client, out_ports[i], dst) == 0) {
                std::cout << "  Connecting: " << out_ports[i] << " -> " << dst << std::endl;
                ++connected;
            }
        }

        jack_free(out_ports);
    }

    const char** in_ports = jack_get_ports(g_jack_client, nullptr, nullptr, JackPortIsInput);
    if (in_ports != nullptr) {
        std::cout << "Connecting output ports..." << std::endl;
        int connected = 0;

        for (int i = 0; in_ports[i] != nullptr && connected < 2; ++i) {
            if (std::strstr(in_ports[i], client_name) != nullptr) continue;

            const char* src =
                (connected == 0) ? jack_port_name(g_output_port_left)
                                 : jack_port_name(g_output_port_right);

            if (jack_connect(g_jack_client, src, in_ports[i]) == 0) {
                std::cout << "  Connecting: " << src << " -> " << in_ports[i] << std::endl;
                ++connected;
            }
        }

        jack_free(in_ports);
    }
}

// =========================
// JACK init / shutdown
// =========================

bool initialize_jack(const char* model_path, const char* ir_path) {
    std::cout << "Opening JACK client..." << std::endl;
    std::cout.flush();

    jack_status_t status{};
    const char* requested_name = "rena800";
    g_jack_client = jack_client_open(requested_name, JackNoStartServer, &status);

    std::cout << "jack_client_open returned, checking if null..." << std::endl;
    std::cout.flush();

    if (g_jack_client == nullptr) {
        std::cerr << "Failed to open JACK client. Status: " << status << std::endl;
        return false;
    }

    const char* actual_name = jack_get_client_name(g_jack_client);

    std::cout << "JACK client opened successfully!" << std::endl;
    std::cout << "Requested JACK client name: " << requested_name << std::endl;
    std::cout << "Actual JACK client name: "
              << (actual_name ? actual_name : "<null>") << std::endl;

    if (status & JackNameNotUnique) {
        std::cerr << "Warning: JACK reassigned client name because it was not unique." << std::endl;
    }

    jack_nframes_t sample_rate = jack_get_sample_rate(g_jack_client);
    jack_nframes_t buffer_size = jack_get_buffer_size(g_jack_client);
    float latency_ms = (buffer_size / static_cast<float>(sample_rate)) * 1000.0f;

    std::cout << "JACK sample rate: " << sample_rate << " Hz" << std::endl;
    std::cout << "JACK buffer size: " << buffer_size << " frames" << std::endl;
    std::cout << "JACK latency: " << latency_ms << " ms" << std::endl;
    std::cout.flush();

    std::cout << "Initializing DSP chain..." << std::endl;
    g_dsp_chain.initialize(static_cast<float>(sample_rate));
    std::cout << "DSP chain initialized" << std::endl;
    std::cout.flush();

    if (model_path != nullptr) {
        std::cout << "\nLoading NAM model: " << model_path << std::endl;
        std::cout.flush();

        if (!g_nam_model.loadFromFile(model_path)) {
            std::cerr << "Failed to load NAM model!" << std::endl;
            std::cerr << "Continuing with NAM bypassed..." << std::endl;
        } else {
            std::cout << "NAM model loaded, setting to processor..." << std::endl;
            if (!g_dsp_chain.nam().setModel(&g_nam_model)) {
                std::cerr << "Failed to set model to processor!" << std::endl;
            } else {
                std::cout << "NAM model set successfully" << std::endl;
            }
        }
    }

    if (ir_path != nullptr) {
        std::cout << "\nLoading Cabinet IR: " << ir_path << std::endl;
        std::cout.flush();

        if (!g_dsp_chain.cabinet().loadIR(ir_path)) {
            std::cerr << "Failed to load Cabinet IR!" << std::endl;
            std::cerr << "Continuing without cabinet simulation..." << std::endl;
        } else {
            std::cout << "Cabinet IR loaded successfully!" << std::endl;
            std::cout << "IR length: " << g_dsp_chain.cabinet().getIRLength() << " samples" << std::endl;
        }
    }

    jack_set_process_callback(g_jack_client, jack_process_callback, nullptr);
    jack_set_xrun_callback(g_jack_client, jack_xrun_callback, nullptr);
    jack_on_shutdown(g_jack_client, jack_shutdown_callback, nullptr);

    g_input_port_left = jack_port_register(
        g_jack_client, "input_left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    g_input_port_right = jack_port_register(
        g_jack_client, "input_right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    g_output_port_left = jack_port_register(
        g_jack_client, "output_left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    g_output_port_right = jack_port_register(
        g_jack_client, "output_right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (!g_input_port_left || !g_input_port_right || !g_output_port_left || !g_output_port_right) {
        std::cerr << "Failed to register one or more JACK ports" << std::endl;
        return false;
    }

    if (jack_activate(g_jack_client) != 0) {
        std::cerr << "Failed to activate JACK client" << std::endl;
        return false;
    }

    print_ports();

    if (g_connect_ports) {
        auto_connect_ports();
    } else {
        std::cout << "Auto-connect disabled (--no-connect)" << std::endl;
    }

    std::cout << "\nJACK client initialized. Ready to play!" << std::endl;
    return true;
}

void shutdown_jack() {
    if (g_jack_client != nullptr) {
        jack_client_close(g_jack_client);
        g_jack_client = nullptr;
    }
}

void signal_handler(int /*signal*/) {
    g_running.store(false);
}

// =========================
// Main
// =========================

int main(int argc, char* argv[]) {
    std::cout << "==================================" << std::endl;
    std::cout << "RenaAmp v0.1.0 - Test Mode" << std::endl;
    std::cout << "==================================" << std::endl;

    const char* model_path = nullptr;
    const char* ir_path = nullptr;

    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--bypass-all") {
            g_bypass_all = true;
        } else if (arg == "--no-connect") {
            g_connect_ports = false;
        } else {
            positional.push_back(arg);
        }
    }

    if (!positional.empty()) {
        static std::string model_storage;
        model_storage = absolute_or_keep(positional[0].c_str());
        model_path = model_storage.c_str();
    }

    if (positional.size() > 1) {
        static std::string ir_storage;
        ir_storage = absolute_or_keep(positional[1].c_str());
        ir_path = ir_storage.c_str();
    }

    std::cout << "Checking arguments..." << std::endl;
    std::cout << "Bypass all DSP: " << (g_bypass_all ? "YES" : "NO") << std::endl;
    std::cout << "Auto-connect: " << (g_connect_ports ? "YES" : "NO") << std::endl;

    if (model_path) {
        std::cout << "Model path: " << model_path << std::endl;
    } else {
        std::cout << "Model path: <none>" << std::endl;
    }

    if (ir_path) {
        std::cout << "IR path: " << ir_path << std::endl;
    } else {
        std::cout << "IR path: <none>" << std::endl;
    }

    std::cout << "Setting up signal handlers..." << std::endl;
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!initialize_jack(model_path, ir_path)) {
        return 1;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "TOCA AHORA - Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;

    uint64_t last_xruns = 0;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint64_t xruns = g_xrun_count.load(std::memory_order_relaxed);
        if (xruns != last_xruns) {
            std::cout << "[XRUN] total=" << xruns
                      << " delta=" << (xruns - last_xruns) << std::endl;
            last_xruns = xruns;
        }
    }

    std::cout << "\nShutting down..." << std::endl;
    shutdown_jack();
    std::cout << "Done." << std::endl;
    return 0;
}