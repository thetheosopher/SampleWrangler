---
applyTo: "**/*.h,**/*.hpp,**/*.cpp,**/*.cxx"
---

# C++ Implementation Instructions (SampleWrangler)

## General
- Target C++20.
- Keep modules small; prefer composition over inheritance (except JUCE Components).
- Avoid exceptions in performance-sensitive and audio code paths; handle errors explicitly.

## Threading
- UI runs on JUCE message thread only.
- All SQLite access should be funneled through a single DatabaseService thread (serialize writes).
- Scanning/analysis/wavecache run on a worker ThreadPool with cancellation tokens.
- Audio callback must remain RT-safe: no allocations/locks/I/O/logging.

## Audio Engine
- Maintain a command queue from UI to audio engine (lock-free ring buffer if possible).
- Decode audio on background threads; audio thread consumes buffers.
- Pitch shifting for MVP: resample-style is acceptable (changes duration).

## Logging
- No logging in audio callback.
- Use lightweight logging elsewhere, and allow it to be compiled out for Release.

## Windows Integration
- Provide “Reveal in Explorer” and drag-drop out.
- Network sources may be offline; scanning must time out and not block UI.

## Code Layout Conventions
- Prefer `namespace sw` (SampleWrangler) for non-JUCE code.
- Keep JUCE types near UI/audio; keep STL types in catalog/pipeline.