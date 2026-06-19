# Renamp Commenting Progress

This document tracks files that have been reviewed and updated to follow the Commenting Style Guide (docs/commenting-style.md). Comment-only changes; no logic or build changes.

## Status legend
- ✅ Done: Header and key "why" comments in place; consistent tone (US English), RT-safety/threading captured.
- ⏳ Planned/Not started: No work yet or pending scope.
- 🔶 Partial: Some fields present but incomplete vs style guide.

## Reviewed (✅)

### include/
- include/audio_engine.h — Renamp header (stub), @brief on class + all public methods, @pre/@post on init/shutdown, `m_`→`_` trailing convention fixed
- include/dsp/cabinet_processor.h — Full Renamp header (RT safety, memory ownership, threading, processing model, design constraints, future path); @brief + ownership annotations on private members
- include/dsp/dsp_chain.h — ✅ Full Renamp header (purpose, RT safety, threading model, runtime order with date); @brief/@pre/@post on initialize/process; @note on position for master gain; design note on why all components initialized; @note on const accessors (UI only)
- include/dsp/gate.h — Full Renamp header (RT safety, memory ownership, threading, design decisions, trade-offs, future path); NOT RT-safe warnings on visualization methods; ownership annotations on private members
- include/dsp/limiter.h — Full Renamp header (RT safety, memory ownership, threading, design decisions on soft-knee, oversampling strategy, known implementation note on computeGain, future path); NOT RT-safe warning on getGainReduction; private ownership rule block
- include/dsp/nam_processor.h — Full Renamp header (RT safety, memory ownership with model lifetime note, threading with release/acquire semantics, denormal protection strategy, dual-backend design decisions, DC blocking rationale, known constraints, future path); NOT RT-safe warnings on setModel/init/info methods; ModelSlot ownership docs; fastTanh error bound documented
- include/dsp/nam_rtneural_converter.h — ✅ Full Renamp header (purpose, RT safety NOT, threading control-only, status partially implemented); @brief with NAM/RTNeural format comparison; @pre/@post with TODO notes; @note on non-RT usage
- include/dsp/rtneural_wrapper.h — ✅ Full Renamp header (purpose, RT safety for process/reset only, threading with atomic ready flag, status partially implemented); @brief with PIMPL design note; @pre/@post with TODO notes on weight loading; @note on passthrough status
- include/dsp/saturation.h — ✅ Full Renamp header (purpose, RT safety, threading model, DSP chain position); @brief with features list; @pre/@post on initialize/process; processing pipeline documented in @note; parameter ranges corrected (drive: 0-10); @note on DC blocking filter formula
- include/models/nam_model.h — ✅ Full Renamp header (purpose, RT safety distinction, threading with atomic ready flag); @brief for NAMArchitecture, NAMConfig, NAMMetadata; @brief/@pre/@post for NAMModel/NAMModelManager methods; @note on RT-safe vs non-RT methods; member comments with ///< style; NAMModelManager threading documented (2026-06-18)

### Root
- src/main.cpp — lifecycle, JACK callbacks, RT-safety
- src/load_and_run.cpp — CLI utility, RT note
- src/list_ca_devices.cpp — utility (CoreAudio)
- src/list_jack_ports.cpp — utility (JACK)

### core/
- src/core/audio_engine.cpp — placeholder clarified (purpose/RT)
- src/core/audio_thread.cpp — placeholder clarified (purpose/RT)
- src/core/control_thread.cpp — placeholder clarified (purpose/threading)
- src/core/loader_thread.cpp — placeholder clarified (purpose/RT)
- src/core/watchdog_thread.cpp — placeholder clarified (purpose/RT)

### dsp/
- src/dsp/dsp_chain.cpp — debug metering note (hot path), order reference
- src/dsp/cabinet_processor.cpp — ✅ Header with RT/threading, IR handling; processChannel convolution optimized: section boundary with design rationale, PREFETCH_DISTANCE explained (~3u for SIMD), narrative comments reduced, technical notes improved (2026-06-18)
- src/dsp/gate.cpp — header with RT/threading, smoothing intent
- src/dsp/limiter.cpp — header + design note; TODO about threshold units review
- src/dsp/nam_processor.cpp — header with RT/threading, chain position
- src/dsp/nam_rtneural_converter.cpp — non-RT conversion/logging note
- src/dsp/rtneural_wrapper.cpp — header with RT note
- src/dsp/saturation.cpp — header with RT notes

### models/
- src/models/ir_model.cpp — placeholder clarified (purpose/RT)
- src/models/model_manager.cpp — placeholder clarified (purpose/threading)
- src/models/nam_model.cpp — header (parse/validate, RT note)

### parameters/
- src/parameters/param_manager.cpp — placeholder clarified (purpose/threading)
- src/parameters/param_smoother.cpp — header with threading model
- include/parameters/param_smoother.h — ✅ Full Renamp header (purpose, RT safety for next/setTarget, threading model with cross-thread communication); @brief with usage example and threading notes; @pre/@post on init/setTarget/next; member comments with ///< style; MultiParameterSmoother documented (2026-06-18)

### threading/
- src/threading/lock_free_queue.cpp — placeholder clarified (purpose/RT)
- src/threading/memory_pool.cpp — placeholder clarified (purpose/RT)

## Planned / Not started (⏳)
- None at this time. Future passes may add targeted “why” comments only where non-obvious.

---

## Documentation Updates (2026-06-18)

### README.md
- Enriched for GitHub portfolio presentation
- Added platform status table with macOS 15.4.1 (24E263) verification
- Added quick-start section with build/run instructions
- Added technical highlights (RT-safety, threading, parameter smoothing)
- Added project structure overview
- Added known limitations and roadmap
- Added public code section (portfolio demonstration)
- Linked to ARCHITECTURE.md and commenting guides

---

Last updated: 2026-06-18

---

## Files Completed in This Session
- include/dsp/dsp_chain.h — Added @pre/@post to initialize/process, @note on master gain position, design note on initializing disconnected stages, @note on const accessors (2026-06-18)
- include/dsp/saturation.h — Added full file header (purpose, RT safety, threading, DSP position), @pre/@post on initialize/process, processing pipeline documented, parameter ranges corrected (drive: 0-10), @note on DC blocking formula (2026-06-18)
- include/dsp/nam_rtneural_converter.h — Added full file header (purpose, non-RT safety note, threading, status partially implemented), @brief with NAM/RTNeural format comparison, @pre/@post with TODO notes on incomplete implementation, @note on non-RT usage (2026-06-18)
- include/dsp/rtneural_wrapper.h — Added full file header (purpose, RT safety for process/reset only, threading with atomic ready flag, status partially implemented), @brief with PIMPL design note, @pre/@post with TODO notes on weight loading placeholder, @note on passthrough status, atomic semantics documented (2026-06-18)
- include/models/nam_model.h — Added full file header (purpose, RT safety distinction, threading with atomic ready flag), @brief for NAMArchitecture/NAMConfig/NAMMetadata, @pre/@post for NAMModel/NAMModelManager methods, @note on RT-safe vs non-RT methods, member comments with ///< style, NAMModelManager threading documented (2026-06-18)
- include/parameters/param_smoother.h — Added full file header (purpose, RT safety for next/setTarget, threading model with cross-thread communication), @brief with usage example and threading notes, @pre/@post on init/setTarget/next, member comments with ///< style, MultiParameterSmoother documented (2026-06-18)
- src/dsp/cabinet_processor.cpp — processChannel convolution: section boundary with design rationale, PREFETCH_DISTANCE explained, narrative comments reduced per style guide (2026-06-18)
- src/dsp/nam_processor.cpp — lstmForwardPass: removed narrative comments (gate labels, “LSTM equations”), consolidated design block, kept only RT-performance notes (fastSigmoid/fastTanh), weight layout documented (2026-06-18)
- src/parameters/param_smoother.cpp — Added design block explaining linear ramp rationale, threading model (lock-free atomic flag), and why smoothing prevents zipper noise (2026-06-18)
