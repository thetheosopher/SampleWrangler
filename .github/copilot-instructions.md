# SampleWrangler (JUCE, Windows, MVP) — Copilot Instructions

## Project Summary
SampleWrangler is a Windows desktop application (Windows 10+) built with JUCE and modern C++.
Goal: a fast audio sample librarian MVP with:
- Root folder management + incremental scanning
- SQLite catalog + fast search
- Waveform overview caching (peaks) + simple waveform viewer
- ASIO preview playback (required) with minimal sampler-like preview features (pitch via MIDI + on-screen keyboard, loop points, basic polyphony)
- No REX/REX2/NKI playback in MVP (index-only)
UI can be minimal/functional for MVP.

## Tech Stack
- Language: C++20
- Framework: JUCE (CMake-based build; avoid Projucer)
- Database: SQLite (embedded)
- Build: CMake (>= 3.15) with MSVC toolchain on Windows (no MinGW). [3](https://ccrma.stanford.edu/~jos/juce_modules/md__Users_jos_w_JUCEModulesDoc_docs_CMake_API.html)[4](https://forum.juce.com/t/cmake-windows/64753)

## Non-negotiable Real-time Audio Rules
When writing audio callback code:
- NO heap allocations
- NO blocking locks/mutexes
- NO file I/O
- NO logging
- Prefer lock-free queues and preallocated buffers/voices
- Any decoding/analysis happens on worker threads; audio callback consumes prepared buffers.

## Build/Run (Windows)
Use CMake Presets (CMakePresets.json).
Typical workflow:
- Configure preset (e.g., `vs2022-debug`)
- Build preset
- Run the built executable from the preset build output folder

## Repository Architecture (Target)
- Source/App: JUCE application entry and AppState
- Source/UI: minimal browser + waveform + preview controls
- Source/Catalog: SQLite schema/migrations, queries
- Source/Pipeline: scanner/analyzer/wave-cache jobs + cancellation + priorities
- Source/Audio: audio engine, voice manager, MIDI routing, pitch/resample
- Source/Util: logging, path utilities

## Data Model Rules
- Catalog is local-only.
- Support moving library roots (relink): store `root_id + relative_path` rather than only absolute paths.
- Backup/restore: copy DB + settings + waveform cache.

## Audio File Formats (MVP)
Playable: WAV (incl ACID), AIFF (incl Apple Loop), FLAC, MP3.
Index-only: REX/REX2, NKI, SFZ.

### MP3 note
JUCE `MP3AudioFormat` requires enabling `JUCE_USE_MP3AUDIOFORMAT` and includes a legal disclaimer; be explicit if enabling it and document the choice. [5](https://docs.juce.com/develop/classjuce_1_1MP3AudioFormat.html)

## Testing/Validation Expectations
- Add lightweight unit tests for non-audio modules where feasible (DB queries, path relinking, cache key logic).
- Always run a build before finishing a change.
- Keep changes small and compile-able.

## Coding Style
- Prefer clear, maintainable C++20
- RAII everywhere; avoid raw owning pointers
- Use `std::unique_ptr` and `std::optional` appropriately
- Keep UI thread, DB thread, worker threads, and audio thread responsibilities separate.