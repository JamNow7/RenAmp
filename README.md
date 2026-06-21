# Renamp

[![CI](https://github.com/JamNow7/RenAmp/workflows/RenAmp%20CI/badge.svg)](https://github.com/JamNow7/RenAmp/actions)
[![Tests](https://github.com/JamNow7/RenAmp/workflows/RenAmp%20CI/badge.svg)](https://github.com/JamNow7/RenAmp/actions)

> Real-time guitar amplifier simulator written in modern C++ for macOS, featuring Neural Amp Modeler (NAM) integration and cabinet impulse response processing.

**Current Status:** Stable real-time prototype. Verified on macOS 15.4.1 (24E263). **✅ CI Tests Passing**

**Copyright (c) 2026 Claudio Cataldo. All rights reserved.**

---

## Overview

Renamp is a low-latency audio application that loads Neural Amp Modeler (NAM) models and cabinet impulse responses (IRs) to reproduce guitar amplifier tones in real time. The project demonstrates real-time DSP engineering, lock-free parameter updates, and modern C++ architecture with RT-safe audio callbacks.

This project serves both as a functional guitar amp simulator and as a portfolio project demonstrating:

- Real-time audio DSP and lock-free threading
- RT-safe audio pipelines (no allocations/locks/I/O in callbacks)
- Cross-thread parameter smoothing to prevent zipper noise
- Modern C++17/20 architecture
- Neural network-based amplifier modeling

---

## Key Features

- **Real-time NAM Integration**: Supports LSTM and WaveNet architectures via NeuralAmpModelerCore
- **Cabinet IR Processing**: Stereo convolution with double-buffered IR swapping
- **RT-Safe DSP Chain**: No dynamic allocations, locks, or blocking operations in audio path
- **Parameter Smoothing**: Cross-thread atomic parameter updates prevent zipper noise
- **JACK Audio Backend**: JACK integration with XRUN monitoring
- **Master Gain Control**: Post-NAM/IR gain with sample-rate-aware smoothing
- **Optional Limiter**: Soft-knee peak limiter to prevent inter-sample peaks

---

## Current DSP Chain

```
Input → NAM → Cabinet IR → Master Gain → [Limiter (optional)] → Output
```

**Processing Order (2025-06-18):**
1. NAM model inference (LSTM or WaveNet)
2. Cabinet impulse response convolution
3. Master gain (dB smoothed, post-NAM/IR)
4. Optional limiter (disabled by default)

---

## Platform Status

| Platform | Status | Notes |
|----------|--------|-------|
| macOS 15.4.1 (24E263) | ✅ Verified | Primary development platform |
| Linux | ⏳ Planned | Raspberry Pi deployment in roadmap |

**Tested Configuration:**
- macOS Sequoia 15.4.1 (24E263)
- Apple Silicon (M-series) with ARM NEON optimization
- JACK Audio Connection Kit
- Release builds at 48 kHz, 64-128 frame buffers

---

## Quick Start

### Prerequisites

- **JACK Audio Connection Kit** (macOS/Linux)
- **CMake** (3.15+) and **C++17** compiler
- **Apple Silicon** (ARM NEON) or x86_64 with SSE

### Build

```bash
mkdir build && cd build
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Testing

```bash
mkdir build && cd build
cmake -DRENAAMP_BUILD_TESTS=ON -DRENAAMP_BUILD_BENCHMARKS=OFF ..
make renaamp_tests
./renaamp_tests -s
```

**Current Test Status:** ✅ NAM tests passing (24 cases, 6542 assertions) | ⚠️ Cabinet/limiter tests have known implementation bugs (9 failures)

Run specific test tags:
```bash
./renaamp_tests "[nam]"     # NAM model tests only (24 tests)
./renaamp_tests "[dsp]"     # DSP tests only
./renaamp_tests "[cabinet]" # Cabinet IR tests
./renaamp_tests "[limiter]" # Limiter tests
```

---

### Run

The project includes a launcher script that handles JACK initialization, model loading, and automatic shutdown:

```bash
chmod +x ./scripts/start_nam_ir_gain.sh
./scripts/start_nam_ir_gain.sh
```

**With custom options:**
```bash
./scripts/start_nam_ir_gain.sh -g +9              # Set master gain to +9 dB
./scripts/start_nam_ir_gain.sh -p 128              # Use 128-frame buffers
./scripts/start_nam_ir_gain.sh -m model.nam -i ir.wav  # Custom model/IR
```

### Manual Execution

```bash
./build/load_and_run <model.nam> <cabinet.wav> [--master-gain <dB>] [--limiter on|off]
```

---

## Technical Highlights

### Real-Time Safety

The audio callback maintains deterministic low-latency processing:

- ✅ No dynamic memory allocations
- ✅ No locks or mutexes
- ✅ No file I/O or logging
- ✅ No blocking system calls

### Threading Architecture

Renamp uses clear separation between:

- **Control thread**: Parameter updates, model/IR loading
- **Audio thread**: DSP execution, JACK callbacks

Cross-thread communication uses lock-free atomics and double-buffered model/IR swapping.

### Parameter Smoothing

User-controlled parameters are smoothed before reaching the audio path to eliminate zipper noise. The `ParameterSmoother` provides linear ramping with configurable duration, updated via atomic flags from the control thread.

### Signal Processing

- **Convolution**: Circular buffer with ARM NEON SIMD optimization and L1 cache prefetching
- **NAM Inference**: LSTM forward pass with fastSigmoid/fastTanh approximations for RT performance
- **Limiter**: Soft-knee design with 3 regions (below knee, quadratic transition, hard limiting)
- **DC Blocking**: First-order highpass filter (~5 Hz) removes neural model DC offset

---

## Project Structure

```
Renamp/
├── src/
│   ├── main.cpp              # Entry point (development/testing)
│   ├── load_and_run.cpp      # Primary JACK runtime
│   ├── dsp/                  # Signal processors (NAM, Cabinet, Limiter, Gate, Saturation)
│   ├── models/               # NAM/IR model loaders and validators
│   ├── parameters/           # Parameter smoothers and managers
│   ├── core/                 # Future audio engine, threading (placeholders)
│   └── threading/            # Lock-free queue, memory pool
├── include/                   # Headers with Doxygen documentation
├── docs/
│   ├── ARCHITECTURE.md       # Complete architecture overview
│   ├── commenting-style.md   # Code documentation guide
│   └── commenting-progress.md # Documentation progress tracking
└── external/                 # NeuralAmpModelerCore, RTNeural, Eigen
```

---

## Documentation

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** — Complete architecture, signal flow, and troubleshooting
- **[Commenting Guide](docs/commenting-style.md)** — Code documentation conventions
- **[Commenting Progress](docs/commenting-progress.md)** — Documentation completion status

---

## Known Limitations

- **WaveNet CPU Load**: WaveNet models are significantly heavier than LSTM models; Release builds recommended
- **IR Length**: Cabinet IRs truncated to 256 samples by default (configurable)
- **JACK Buffer Size**: Dynamic buffer-size changes not fully handled internally
- **Platform**: Verified only on macOS 15.4.1 (24E263); Linux support in development

---

## Roadmap

- [ ] Raspberry Pi deployment (embedded Linux)
- [ ] MIDI control for parameters
- [ ] Preset management system
- [ ] EQ module
- [ ] Delay effects
- [ ] Plugin formats (VST/AU)

---

## Public Code

This project is publicly available as a portfolio demonstration of real-time audio systems and modern C++ development. The codebase showcases production-ready patterns for DSP engineering, RT-safe threading, and neural network-based audio processing.

---

## Author

**Claudio Cataldo**

Software Developer • Guitarist

---

This project uses third-party libraries with their own licenses:

- **NeuralAmpModelerCore**: Apache-2.0
- **RTNeural**: MIT
- **Eigen**: MPL-2.0

See `external/` directories for details.
