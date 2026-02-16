# Agent Instructions (SampleWrangler MVP)

When acting as a coding agent in this repo:

## Goals
- Keep the project buildable at all times.
- Prioritize a working MVP: scan -> catalog -> search -> waveform -> ASIO preview.
- Implement minimal UI (functional over polished).

## Constraints
- Windows-only MVP.
- ASIO support is required for MVP (use JUCE AudioDeviceManager).
- No REX/REX2/NKI playback in MVP (index-only).

## Workflow
1. Before coding: identify the target module (UI/Catalog/Pipeline/Audio).
2. Make small cohesive commits.
3. Always ensure changes compile via the documented CMake preset build.
4. Do not add heavy dependencies without updating docs and build scripts.

## Real-time Audio Rules
- Never allocate, lock, log, or do I/O in the audio callback.
- Background threads do decoding and analysis; callback consumes prepared buffers.

## Output Expectations
- Prefer writing code plus minimal documentation updates in the same change.
- Include a brief "How to test" note in PR descriptions.