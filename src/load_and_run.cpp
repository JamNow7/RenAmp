/*
 * Renamp — Load and Run (utility)
 * Purpose: CLI to load a NAM model and IR and process audio via JACK.
 * RT note: logs and file I/O occur outside the audio callback; callback path remains RT-safe.
 */
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>

#include <jack/jack.h>

#include "dsp/dsp_chain.h"
#include "models/nam_model.h"

// Global state
std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_xrun_count{0};
RenaAmp::DSPChain g_dsp_chain;
RenaAmp::NAMModel g_nam_model;
std::string g_ir_path;  // IR file path

// JACK client and ports
jack_client_t* g_jack_client = nullptr;
jack_port_t* g_input_port_left = nullptr;
jack_port_t* g_input_port_right = nullptr;
jack_port_t* g_output_port_left = nullptr;
jack_port_t* g_output_port_right = nullptr;

// JACK callback - RT-safe
int jack_process_callback(jack_nframes_t nframes, void* arg) {
    // Get buffers
    jack_default_audio_sample_t* in_left = reinterpret_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_input_port_left, nframes));
    jack_default_audio_sample_t* in_right = reinterpret_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_input_port_right, nframes));
    jack_default_audio_sample_t* out_left = reinterpret_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_output_port_left, nframes));
    jack_default_audio_sample_t* out_right = reinterpret_cast<jack_default_audio_sample_t*>(
        jack_port_get_buffer(g_output_port_right, nframes));

    // Copy input to output
    std::memcpy(out_left, in_left, nframes * sizeof(jack_default_audio_sample_t));
    std::memcpy(out_right, in_right, nframes * sizeof(jack_default_audio_sample_t));

    // Process through DSP chain
    g_dsp_chain.process(out_left, out_right, nframes);

    return 0;
}

void jack_shutdown_callback(void* arg) {
    std::cerr << "\nJACK server shut down. Exiting..." << std::endl;
    g_running.store(false);
}

int jack_xrun_callback(void* /*arg*/) {
    g_xrun_count.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

bool initialize_jack(const char* model_path, const char* ir_path) {
    std::cout << "Opening JACK client..." << std::endl;
    std::cout.flush();

    // Open JACK client with unique name, don't start server
    jack_status_t status;
    g_jack_client = jack_client_open("renaamp_jcm800", JackNoStartServer, &status);

    std::cout << "jack_client_open returned, checking if null..." << std::endl;
    std::cout.flush();

    if (g_jack_client == nullptr) {
        std::cerr << "Failed to open JACK client. Status: " << status << std::endl;
        return false;
    }

    std::cout << "JACK client opened successfully!" << std::endl;
    std::cout.flush();

    // Get sample rate
    jack_nframes_t sample_rate = jack_get_sample_rate(g_jack_client);
    jack_nframes_t buffer_size = jack_get_buffer_size(g_jack_client);
    float latency_ms = (buffer_size / static_cast<float>(sample_rate)) * 1000.0f;

    std::cout << "JACK sample rate: " << sample_rate << " Hz" << std::endl;
    std::cout << "JACK buffer size: " << buffer_size << " frames" << std::endl;
    std::cout << "JACK latency: " << latency_ms << " ms" << std::endl;
    std::cout.flush();

    // Initialize DSP chain
    std::cout << "Initializing DSP chain..." << std::endl;
    std::cout.flush();
    g_dsp_chain.initialize(static_cast<float>(sample_rate));
    std::cout << "DSP chain initialized" << std::endl;
    std::cout.flush();

    // Load NAM model
    if (model_path != nullptr) {
        std::cout << "\nLoading NAM model: " << model_path << std::endl;
        std::cout.flush();

        if (!g_nam_model.loadFromFile(model_path)) {
            std::cerr << "Failed to load NAM model!" << std::endl;
            std::cerr << "Continuing with NAM bypassed..." << std::endl;
        } else {
            std::cout << "NAM model loaded, setting to processor..." << std::endl;
            std::cout.flush();

            if (!g_dsp_chain.nam().setModel(&g_nam_model)) {
                std::cerr << "Failed to set model to processor!" << std::endl;
            } else {
                std::cout << "NAM model set successfully" << std::endl;
            }
        }
    }

    // Load Cabinet IR
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

    // Register callbacks
    jack_set_process_callback(g_jack_client, jack_process_callback, nullptr);
    jack_set_xrun_callback(g_jack_client, jack_xrun_callback, nullptr);
    jack_on_shutdown(g_jack_client, jack_shutdown_callback, nullptr);

    // Register ports
    g_input_port_left = jack_port_register(g_jack_client, "input_left",
                                           JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    g_input_port_right = jack_port_register(g_jack_client, "input_right",
                                            JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    g_output_port_left = jack_port_register(g_jack_client, "output_left",
                                            JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    g_output_port_right = jack_port_register(g_jack_client, "output_right",
                                             JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    // Activate client
    if (jack_activate(g_jack_client) != 0) {
        std::cerr << "Failed to activate JACK client" << std::endl;
        return false;
    }

    // Auto-connect to system ports (TSMEGA or system)
    // IMPORTANT: Filter out our own ports to avoid feedback loops
    const char* client_name = jack_get_client_name(g_jack_client);

    // DEBUG: List all available ports
    std::cout << "\n=== Available JACK Ports ===" << std::endl;

    const char** all_ports = jack_get_ports(g_jack_client, nullptr, nullptr, 0);
    if (all_ports != nullptr) {
        for (int i = 0; all_ports[i] != nullptr; i++) {
            int flags = jack_port_flags(jack_port_by_name(g_jack_client, all_ports[i]));
            const char* type = (flags & JackPortIsInput) ? "INPUT" : "OUTPUT";
            std::cout << "  [" << type << "] " << all_ports[i] << std::endl;
        }
        jack_free(all_ports);
    }
    std::cout << "=============================\n" << std::endl;

    // Connect system outputs to our inputs
    const char** ports = jack_get_ports(g_jack_client, nullptr, nullptr, JackPortIsOutput);
    if (ports != nullptr) {
        std::cout << "Connecting input ports..." << std::endl;
        int connected = 0;
        for (int i = 0; ports[i] != nullptr && connected < 2; i++) {
            // Skip our own ports
            if (std::strstr(ports[i], client_name) == nullptr) {
                const char* port_name = (connected == 0) ? jack_port_name(g_input_port_left) : jack_port_name(g_input_port_right);
                std::cout << "  Connecting: " << ports[i] << " -> " << port_name << std::endl;
                jack_connect(g_jack_client, ports[i], port_name);
                connected++;
            }
        }
        jack_free(ports);
    }

    // Connect our outputs to system inputs
    ports = jack_get_ports(g_jack_client, nullptr, nullptr, JackPortIsInput);
    if (ports != nullptr) {
        std::cout << "Connecting output ports..." << std::endl;
        int connected = 0;
        for (int i = 0; ports[i] != nullptr && connected < 2; i++) {
            // Skip our own ports
            if (std::strstr(ports[i], client_name) == nullptr) {
                const char* port_name = (connected == 0) ? jack_port_name(g_output_port_left) : jack_port_name(g_output_port_right);
                std::cout << "  Connecting: " << port_name << " -> " << ports[i] << std::endl;
                jack_connect(g_jack_client, port_name, ports[i]);
                connected++;
            }
        }
        jack_free(ports);
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

void signal_handler(int signal) {
    std::cout << "\nShutting down..." << std::endl;
    g_running.store(false);
}

int main(int argc, char* argv[]) {
    std::cout << "==================================" << std::endl;
    std::cout << "RenaAmp v0.1.0 - Test Mode" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout.flush();

    const char* model_path = nullptr;
    const char* ir_path = nullptr;

    std::cout << "Checking arguments..." << std::endl;
    std::cout.flush();

    if (argc > 1) {
        model_path = argv[1];
        std::cout << "Model path: " << model_path << std::endl;
    } else {
        std::cout << "\nUsage: " << argv[0] << " <path_to_model.nam> [path_to_cabinet_ir.wav]" << std::endl;
        std::cout << "Running without NAM model (clean signal)..." << std::endl;
    }

    if (argc > 2) {
        ir_path = argv[2];
        std::cout << "IR path: " << ir_path << std::endl;
    }

    // Optional flags: --master-gain <dB>, --limiter on|off
    float master_gain_db = 0.0f;
    bool limiter_on = false; // default off for guitar workflow
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--master-gain") == 0 && (i + 1) < argc) {
            master_gain_db = std::strtof(argv[i + 1], nullptr);
            i++;
        } else if (std::strcmp(argv[i], "--limiter") == 0 && (i + 1) < argc) {
            const char* v = argv[i + 1];
            limiter_on = (std::strcmp(v, "on") == 0 || std::strcmp(v, "ON") == 0 || std::strcmp(v, "1") == 0);
            i++;
        }
    }

    std::cout.flush();

    std::cout << "Setting up signal handlers..." << std::endl;
    std::cout.flush();

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!initialize_jack(model_path, ir_path)) {
        return 1;
    }

    // Apply master gain (post NAM/IR, pre-Limiter)
    g_dsp_chain.setMasterGain(master_gain_db, 0.02f);
    // Enable/disable limiter
    g_dsp_chain.setLimiterEnabled(limiter_on);

    std::cout << "\n========================================" << std::endl;
    std::cout << "TOCA AHORA - Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;

    uint64_t last_xruns = 0;
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        uint64_t xr = g_xrun_count.load(std::memory_order_relaxed);
        if (xr != last_xruns) {
            std::cout << "[XRUN] total=" << xr << " delta=" << (xr - last_xruns) << std::endl;
            last_xruns = xr;
        }
    }

    shutdown_jack();
    std::cout << "Done." << std::endl;
    return 0;
}
