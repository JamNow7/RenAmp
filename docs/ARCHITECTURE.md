⸻

# RenAmp Architecture & Operational Notes

> **Updated:** Jun 18, 2026

## Contents

- [Repository Purpose](#repository-purpose)
- [What RenAmp Is Today](#what-renamp-is-today)
- [Current Development Stage](#current-development-stage)
- [Current DSP Chain](#current-dsp-chain)
- [Backend and Executables](#backend-and-executables)
- [Stabilization Work Completed](#stabilization-work-completed)
- [Recommended Workflow](#recommended-workflow)
- [Quick Tuning](#quick-tuning)
- [Troubleshooting](#troubleshooting)
- [Module Status](#module-status)
- [Immediate Roadmap](#immediate-roadmap)
- [Known Constraints](#known-constraints)
- [Signal Flow Summary](#signal-flow-summary)

⸻

## Repository Purpose

This document is the source of truth for the current **RenAmp** implementation.

If the README and the code disagree, trust the code and update this document accordingly.

## What RenAmp Is Today

## Current Development Stage

Stable real-time prototype.

### Verified

- Real-time NAM execution
- Cabinet IR convolution
- JACK/CoreAudio integration
- XRUN monitoring
- Automatic launcher script

### Not Yet Implemented

- MIDI control
- Preset management
- Plugin formats (VST/AU)
- Embedded deployment

- A real-time Neural Amp Modeler (NAM) loader and runner with cabinet impulse response (IR) simulation running on JACK/CoreAudio (macOS).
- It is not a pedalboard or preamp simulator.
- Gate and Saturation modules exist in the codebase but are not currently connected to the active signal path.

## Current DSP Chain

**Verified signal path:**

```text
Input → NAM → Cabinet (IR) → MasterGain → [Limiter (optional, OFF by default)] → Output
```

## Components

### NAM (`src/dsp/nam_processor.*`)

- Supports LSTM and WaveNet models through `NeuralAmpModelerCore`.
- Double-buffered model loading and swapping.
- Internal input gain.
- Internal output gain.
- Mix control.

### Cabinet (`src/dsp/cabinet_processor.*`)

- Direct convolution with stereo impulse responses.
- Double-buffered IR swapping.
- Automatic normalization.
- Dry/wet mix.
- IR truncated to 256 samples by default (configurable in code).

### MasterGain (`src/dsp/dsp_chain.*`)

- Gain in dB.
- Sample-rate-aware smoothing.
- Applied after NAM and Cabinet processing.
- Applied before Limiter.

### Limiter (`src/dsp/limiter.*`)

- Present and functional.
- Disabled by default.
- Can be enabled through CLI.

### Gate (`src/dsp/gate.*`)

- Implemented.
- Not connected to the active DSP chain.

### Saturation (`src/dsp/saturation.*`)

- Implemented.
- Not connected to the active DSP chain.

## Backend and Executables

### Executables

| Executable | Purpose |
|------------|---------|
| `load_and_run` | Main real-time runtime |
| `renaamp` | Secondary development/testing executable |

### Primary Runtime Executable

#### `load_and_run` (`src/load_and_run.cpp`)

**Responsibilities:**

- Creates JACK client.
- Creates stereo input/output ports.
- Handles NAM loading.
- Handles Cabinet IR loading.
- Runs the real-time audio callback.
- Reports XRUN events.

**Supported CLI flags:**

```bash
--master-gain <dB>
--limiter on|off
```

### Alternative Executable

#### `renaamp` (`src/main.cpp`)

- Exists for development/testing.
- Not part of the recommended workflow.

## Stabilization Work Completed

### Real-Time Safety

- Cabinet processor pre-allocates temporary buffers.
- No runtime buffer reallocations inside audio callbacks.
- `ParameterSmoother` uses actual JACK sample rate.
- Fixed previous hardcoded 48 kHz assumptions.

### CPU Stability

- Added denormal protection (tiny-noise strategy) in NAM processing.
- Prevents CPU spikes during low-level signal tails.

### XRUN Monitoring

- `load_and_run` registers a JACK XRUN callback.

**Example output:**

```text
[XRUN] total=3
```

### Build Configuration

- Release builds are recommended for WaveNet models.
- Debug builds can produce XRUNs at 48 kHz / 64-frame buffers.

## Recommended Workflow

### One-Command Launcher

```bash
./scripts/start_nam_ir_gain.sh
```

**Responsibilities:**

- Verifies JACK availability.
- Starts JACK automatically if necessary.
- Uses CoreAudio backend on macOS.
- Builds `load_and_run` automatically if missing.
- Loads default NAM model.
- Loads default Cabinet IR.
- Applies `--master-gain +6 dB`.
- Stops the JACK server it started when execution ends.

### First-Time Setup

```bash
chmod +x ./scripts/start_nam_ir_gain.sh
```

### Run

```bash
./scripts/start_nam_ir_gain.sh
```

### Useful Options

```bash
./scripts/start_nam_ir_gain.sh -g +9
./scripts/start_nam_ir_gain.sh -p 128
./scripts/start_nam_ir_gain.sh -m <model.nam> -i <ir.wav>
./scripts/start_nam_ir_gain.sh --no-start-jack
```

### Expected Output

```text
JACK sample rate: 48000 Hz
JACK buffer size: 64 frames
Loaded WaveNet model ...
Cabinet IR loaded successfully!
```

## Quick Tuning

### More Output Level

Increase master gain:

```bash
./scripts/start_nam_ir_gain.sh -g +9
```

This changes output level without changing model drive characteristics.

### XRUNs

If XRUNs appear repeatedly:

```bash
./scripts/start_nam_ir_gain.sh -p 128
```

**Recommended settings:**

- Release build.
- 48 kHz.
- 64–128 frame buffers.

## Minimal Troubleshooting

### No Sound

Verify JACK connections:

```bash
jack_lsp -c
```

Test audio passthrough:

```bash
./build/load_and_run
```

A clean guitar signal should pass through.

### Crash During Startup

**Recommendations:**

- Start JACK at 48 kHz.
- Use 64 or 128 frame buffers.
- Use Release builds.
- Avoid unusual JACK configurations when testing WaveNet models.

### Occasional Crackles

**Most common causes:**

- Debug builds.
- Buffers that are too small.
- CPU overload from WaveNet models.

Use:

```bash
-p 128
```

And run in Release mode.

## Module Status

| Module | Status |
|----------|----------|
| NAM | Active |
| Cabinet | Active |
| MasterGain | Active |
| Limiter | Optional (OFF by default) |
| Gate | Implemented, not connected |
| Saturation | Implemented, not connected |

## Immediate Roadmap

- Expose `--nam-in-gain`.
- Expose `--nam-out-gain`.
- Expose `--ir-mix`.
- Internal NAM block splitting for variable JACK buffer sizes.
- Support longer cabinet IRs with optimized processing.
- Raspberry Pi deployment.

## Known Constraints

- WaveNet models are significantly heavier than LSTM models.
- Current default IR processing uses 256 samples.
- Dynamic JACK buffer-size changes are not yet fully handled internally.
- Longer IR support may require optimized block-based convolution.

## Signal Flow Summary

```text
Input → NAM → Cabinet IR → MasterGain → Limiter (optional) → Output
```

This path is stable, tested, and represents the current operational baseline for future development.