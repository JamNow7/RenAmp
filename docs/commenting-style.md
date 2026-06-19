# Renamp Commenting Style Guide

> Comment intent and constraints, not the obvious. Prioritize maintainers and reviewers.

## Philosophy
- 80% self-explanatory code.
- 20% contextual comments (the why).

## When to Comment (adds context)
Focus comments on:
- Why a decision exists (design intent, key trade-offs, alternatives considered).
- Real-time constraints (RT-safe): no allocations, locks, system calls, or I/O in audio callbacks.
- Threading model: who updates parameters, who runs processing, and how data crosses threads (atomics/lock-free).
- Processing order and stage responsibilities (e.g., NAM → Cabinet → Master Gain → Limiter).
- Performance considerations and measurable constraints (latency, denormals, SIMD/alignment, cache behavior).
- Known limitations and non-goals (what we intentionally do not support yet).

## When NOT to Comment (noise)
Avoid comments that:
- Explain line by line what the code already expresses.
- Repeat names of variables or functions.
- Narrate obvious math or control flow.
- Turn files into long essays; keep comments concise and placed at the right level.

---

## Where Comments Live (purpose and scope)

### File headers (.h/.cpp)
Include only what a new contributor needs to orient quickly:
- Purpose of the module/component.
- RT-safety assumptions for audio callbacks.
- Threading model (control vs audio thread; ownership/lifetime hints).
- Current processing order (when relevant) and responsibilities.
- Important dependencies or limitations.

Template:
```
/*
 * Renamp — <Module or Class>
 * Purpose: <high-level responsibility>.
 * Real-time safety: <no allocs/locks/I/O in process(); accepted primitives>.
 * Threading model: <who updates params, who runs process, cross-thread mechanism>.
 * Processing order: <effective order or reference>.
 * Limitations: <notable constraints or pending QA>.
 */
```

### Public headers (include/*)
- Use concise Doxygen (@brief) for classes and public functions.
- State pre/post-conditions, buffer ownership/lifetime, and expected formats (planar/interleaved, alignment).

Doxygen example:
```
/**
 * @brief Process a block of stereo samples in-place.
 * @param left  Pointer to left-channel buffer (size: count)
 * @param right Pointer to right-channel buffer (size: count)
 * @param count Sample count per channel
 * @pre  Buffers are non-null, 32-bit float; aligned for SIMD if enabled.
 * @post In-place processed audio; no allocations/locks performed.
 * @note RT-safe: callable from the audio thread.
 */
```

### Implementation files (src/*)
- Add intent notes at section boundaries (not per-line narration).
- Document the “why” behind non-obvious constants/approximations/ordering.
- Mark debug/diagnostics clearly and keep them out of the hot path (prefer compile-time guards).

### TODO/FIXME (standardized, searchable)
- Use only for actionable items or defects with impact.
- Format:
  - `TODO(owner:handle, YYYY-MM-DD): short action`
  - `FIXME(owner:handle, YYYY-MM-DD): defect/debt affecting behavior`
  - Optional prefixes: `PERF`, `DOC`, `TEST`.

Examples:
```
TODO(owner:ccataldo, 2026-06-17): Enable Gate/Saturation after NAM post-QA.
FIXME(owner:ccataldo, 2026-06-17): Investigate inter-sample peaks at +12 dB.
PERF(owner:ccataldo, 2026-06-17): Verify denormal handling (FTZ/DAZ) across targets.
```

---

## Good vs Bad (condensed DSP examples)

1) Initialize stages not yet wired
Bad:
```cpp
// Initialize stuff
```
Good:
```cpp
// Initialize all stages even if some are not wired yet.
// Why: keeps parameter surfaces stable and avoids reallocations/races when enabling later.
```

2) Master gain after NAM/IR
Bad:
```cpp
// apply gain
```
Good:
```cpp
// Post-NAM/IR gain to adjust loudness without re-exciting the model/IR.
// 0.1151292546 = ln(10)/20 (dB → linear).
```

3) Optional limiter
Bad:
```cpp
// limiter is optional
```
Good:
```cpp
// Optional limiter; default off for natural dynamics.
// Enable to prevent inter-sample peaks or for strict line-level paths.
```

4) Debug meter
Bad:
```cpp
// temp meter - remove later
```
Good:
```cpp
// Debug-only metering for headroom/RMS/crest; keep out of the audio callback.
// Prefer a compile-time flag.
```

5) Buffer loop narration (avoid)
Bad:
```cpp
// iterate samples; compute abs; update peak; add to rms
for (...) { ... }
```
Good:
```cpp
// Compute block-level peak and RMS for diagnostics (branchless where practical).
```

6) Clamp at output
Bad:
```cpp
// clamp to [-1, 1]
```
Good:
```cpp
// Safety clamp to [-1, 1] when limiter is disabled; not a dynamics processor.
```

---

## Language
- Comments in English; refer to the product as “Renamp”.
- Do not rename identifiers (e.g., namespace) solely for branding.

## Review Checklist (before merging)
- [ ] Comments focus on intent/constraints; no narration.
- [ ] File headers include RT/threading notes where applicable.
- [ ] Public headers have concise Doxygen with pre/post-conditions.
- [ ] Processing order documented and consistent with implementation.
- [ ] Debug/diagnostics marked and kept out of audio callbacks.
- [ ] No comments repeat obvious code or names.

## Scope and Evolution
Keep this guide short. For deeper topics (denormals, IR handling, SIMD alignment), write a focused doc under `docs/` and link it from concise code comments.
