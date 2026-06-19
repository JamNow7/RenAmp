RenaAmp

Real-time guitar amplifier simulator written in modern C++.

**Copyright (c) 2026 Claudio Cataldo. All rights reserved.**

RenaAmp is a low-latency audio application that loads Neural Amp Modeler (NAM) models and cabinet impulse responses (IRs) to reproduce guitar amplifier tones in real time.

The project was built as both a practical audio tool and a demonstration of real-time DSP engineering, low-latency audio processing, and modern C++ architecture.

Highlights

* Real-time audio processing
* Neural Amp Modeler (NAM) integration
* Cabinet impulse response (IR) processing
* RT-safe audio pipeline
* Lock-free parameter updates
* Parameter smoothing to prevent zipper noise
* JACK-based audio I/O
* Cross-platform architecture (macOS/Linux)

Technical Challenges Solved

Real-Time Safety

The audio callback performs:

* No dynamic memory allocations
* No locks or mutexes
* No file I/O
* No blocking operations

This ensures deterministic low-latency audio processing.

Threading Architecture

RenaAmp uses a clear separation between:

* Control thread (parameter updates)
* Audio thread (DSP execution)

Cross-thread communication is handled using RT-safe mechanisms to avoid audio dropouts and priority inversion.

DSP Signal Chain

Current processing order:

Input → NAM → Cabinet IR → Master Gain → Optional Limiter → Output

The signal chain is explicitly documented and designed to be extended with additional processing stages.

Parameter Smoothing

User-controlled parameters are smoothed before reaching the audio path to eliminate zipper noise and maintain stable output levels.

Technologies

* C++17
* CMake
* Neural Amp Modeler (NAM)
* RTNeural
* JACK Audio Connection Kit
* CoreAudio (macOS)
* Modern DSP design patterns

Why This Project Exists

RenaAmp was created to explore real-time audio systems, DSP architecture, and neural-network-based amplifier modeling while maintaining production-style engineering practices.

The project serves both as a functional guitar amp simulator and as a portfolio project demonstrating:

* Audio DSP
* Real-time systems programming
* Thread-safe architecture
* Performance-oriented C++
* Open-source development practices

Build

mkdir build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

Run

./build/load_and_run <model.nam> <cabinet.wav>
