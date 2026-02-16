---
name: scaffold-samplewrangler
description: "Scaffold SampleWrangler (JUCE C++20) Windows MVP audio sample librarian: CMake, modules, DB, scanning, waveform cache, ASIO preview skeleton."
argument-hint: "Optionally provide a short extra requirement or constraint"
agent: "agent"
---

You are working inside ${workspaceFolder}. Follow repository instructions in:
- .github/copilot-instructions.md
- .github/instructions/*.instructions.md

## Objective
Scaffold a buildable JUCE C++20 application (Windows 10+) named **SampleWrangler** that matches the MVP architecture:
- Minimal UI (left: sources/tree, center: results/search, bottom/right: waveform + preview)
- SQLite catalog and schema
- Background scanning pipeline (incremental) with cancellation
- Waveform cache (peak overview) generation on demand
- Audio preview engine skeleton with ASIO selection via JUCE AudioDeviceManager
- MIDI input routing + on-screen keyboard component
- No full sampler editor; preview-only behaviors

## Deliverables (create these files if missing)
1) CMake build:
- CMakeLists.txt (root) that defines an executable target named `SampleWrangler`
- CMakePresets.json (Debug/Release presets using MSVC)
- JUCE as submodule in /JUCE and added via add_subdirectory(JUCE)
2) Source tree:
- Source/App/Main.cpp and MainComponent wiring
- Source/UI/* minimal panels (BrowserPanel, ResultsPanel, WaveformPanel, PreviewPanel)
- Source/Catalog/* (CatalogDb, CatalogSchema, data models)
- Source/Pipeline/* (JobQueue, Scanner, Analyzer, WaveformCache)
- Source/Audio/* (AudioEngine, VoiceManager, MidiInputRouter)
- Source/Util/* (Paths, Hashing, Logging)
3) docs:
- docs/DEVSETUP.md (build/run steps)
4) VS Code helper configs:
- .vscode/settings.json, .vscode/extensions.json, .vscode/launch.json

## Implementation details (MVP defaults)
- Preview playback: manual play/stop (Spacebar); selection does not auto-play by default.
- Pitch shift: resample-style pitch for MVP (duration changes).
- Analysis: on-demand + idle (prioritize selected item).
- Wave cache: per-file cache file keyed by (root_id, rel_path, size, mtime).
- Source relinking: store source table + relative paths, not only absolute file paths.
- Index-only formats: REX/REX2, NKI, SFZ (display + search but no audition).
- Avoid MinGW; assume MSVC.

## SQLite approach
Prefer an embedded SQLite strategy:
- Either vendor sqlite amalgamation into third_party/sqlite (recommended)
- Or use system sqlite if already available; but keep build reproducible.

Create schema tables: roots, files, preview_settings, analysis, wave_cache and FTS table for filename/rel_path search.

## Audio formats
Support WAV/AIFF/FLAC/MP3 in MVP.
If MP3 needs special flags, add a clear CMake option and document it.

## JUCE modules
Include and link required JUCE modules for UI + audio:
juce_core, juce_gui_basics, juce_gui_extra, juce_audio_basics, juce_audio_devices, juce_audio_formats, juce_audio_utils, juce_dsp.

## Quality bar
- Code must compile after scaffolding.
- Put TODOs for non-implemented bodies, but provide enough stubs to build.
- Ensure threading boundaries are explicit and RT-safe constraints are documented in AudioEngine.

## Optional extra constraint from user
${input:extra:MVP extra constraint (optional)}

## Output format
- Create/modify files directly in the workspace.
- Summarize what you created and how to build/run with the configured preset.