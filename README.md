# SampleWrangler

SampleWrangler is a Windows desktop audio sample librarian and preview tool built with JUCE, modern C++, and SQLite. It is aimed at fast local sample browsing: add source folders, scan them into a catalog, search by name or path, inspect waveform metadata, and audition files with low-latency playback, looping, and MIDI-driven pitch preview.

The current codebase is an MVP-focused application rather than a generic framework. It targets Windows 10+, uses CMake with Visual Studio 2022, prefers ASIO on startup when available, and keeps cataloging, scanning, waveform generation, and audio playback separated into distinct modules.

## Highlights

- Source folder management with add, rename, remap, delete, rescan, and reveal-in-Explorer actions.
- SQLite-backed catalog with FTS5 search over file names and relative paths.
- Background scanning and rescanning on a worker queue with cancellation support.
- Metadata-rich results including file size, duration, sample rate, channels, bit depth, bitrate, codec, BPM, key, loop markers, and REX slice count where available.
- Cached waveform overviews for fast redraws in both the results list and the main waveform view.
- Preview playback controls for play, stop, loop, autoplay, pitch transpose, stretch, and optional high-quality stretch.
- MIDI preview from both physical MIDI inputs and the on-screen keyboard.
- Polyphonic preview voice management with RT-safe audio-thread rules in the playback engine.
- Multiple analysis views in the waveform panel: waveform, spectrogram, oscilloscope, and spectrum analyzer.
- Persisted app state for theme, audio device, MIDI input, preview settings, layout, and last selection.
- Database maintenance tools including VACUUM from the toolbar.
- External drag-and-drop of files from the results list into Explorer or other desktop applications.

## What The App Does

SampleWrangler is designed around a simple workflow:

1. Add one or more sample source folders.
2. Scan those folders into a local catalog.
3. Search and filter the indexed files.
4. Select a file to inspect metadata and waveform information.
5. Audition it through the selected audio device, optionally using MIDI or the on-screen keyboard to repitch playback.

The UI is organized into a source browser, a searchable results panel, a waveform display, and a preview/control area.

## Implemented Feature Set

### Library and Catalog

- Local-only catalog stored in SQLite.
- Root/source records stored separately from file records.
- File identity based on source root plus relative path, which makes source-folder remapping practical.
- Fast text search using SQLite FTS5.
- Search scopes include whole-library search and per-root search.
- File statistics are tracked for the entire library, a single root, or the current search result set.

### Scanning and Metadata Extraction

- Recursive scanning of source folders on background worker threads.
- Transaction-batched database updates during scans.
- ACID WAV metadata parsing, including BPM, root note, beats, and loop points when present.
- Apple Loop AIFF metadata parsing, including root note and loop markers.
- REX and RX2 metadata extraction through the bundled REX SDK integration when available.
- Automatic waveform overview generation during scanning for playable files.

### Preview and Audio

- JUCE-based output device management.
- ASIO support enabled on Windows builds.
- Preference for low-latency device setup, including smaller buffer sizes for ASIO devices.
- Play/stop preview workflow with spacebar support.
- Loop playback for the current selection.
- Autoplay option for stepping through results.
- Resample-style pitch shifting in semitones.
- Optional stretch mode and optional high-quality stretch mode when Rubber Band is available.
- Physical MIDI input routing plus an on-screen keyboard component.
- Polyphonic MIDI-triggered preview playback with preallocated voices.

### Waveform and Visual Analysis

- Cached peak-overview waveform rendering.
- Scrubbable main waveform display with playhead and loop overlays.
- Alternate right-click display modes: spectrogram, oscilloscope, and spectrum analyzer.
- Small waveform previews directly in search results.

### Usability

- Dark and light theme support.
- Resizable split layout with persisted panel ratios.
- Toolbar actions for source management, rescanning, canceling scans, resetting layout, and compressing databases.
- Reveal selected source in Windows Explorer.
- Drag indexed files out of the application to external destinations.

## Supported Formats

### Playable

- WAV
- AIFF / AIF
- FLAC
- MP3
- REX / RX2 when the REX SDK is available

### Metadata Notes

- WAV: duration, sample rate, channels, bit depth, bitrate estimate, codec, ACID metadata, and loop markers when present.
- AIFF: duration, sample rate, channels, bit depth, bitrate estimate, codec, Apple Loop metadata, and loop markers when present.
- FLAC and MP3: standard reader metadata available through JUCE.
- REX / RX2: sample rate, channels, bit depth, duration, BPM, and slice count when decoded through the REX SDK.

## Build Requirements

- Windows 10+
- Visual Studio 2022 with the Desktop C++ workload
- CMake 3.15+
- Git

This repository is configured for MSVC and CMake presets. The documented workflow does not target MinGW.

## Quick Start

Clone the repo with submodules:

```bash
git clone https://github.com/thetheosopher/SampleWrangler.git
cd SampleWrangler
git submodule update --init --recursive
```

Configure and build the default debug preset:

```bash
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
```

Run the app:

```bash
.\build\vs2022-debug\SampleWrangler_artefacts\Debug\SampleWrangler.exe
```

Release build:

```bash
cmake --preset vs2022-release
cmake --build --preset vs2022-release
```

Create a release package:

```bash
cmake --preset vs2022-release
cmake --build --preset vs2022-release --target PACKAGE
```

If WiX is installed, packaging produces installer artifacts under `build/vs2022-release/`.

## CMake Presets

- `vs2022-debug`: Debug build with Rubber Band high-quality stretch enabled.
- `vs2022-release`: Release build with Rubber Band high-quality stretch enabled.
- `vs2022-debug-nohq`: Debug build without Rubber Band.
- `vs2022-release-nohq`: Release build without Rubber Band.

## Runtime Data Storage

By default, SampleWrangler stores its local data in the Windows Local AppData area:

- Catalog database: `C:\Users\<user>\AppData\Local\SampleWrangler\catalog.db`
- Waveform cache database: `C:\Users\<user>\AppData\Local\SampleWrangler\wave_cache.db`

The waveform cache is currently stored as SQLite BLOB data rather than loose `.peak` files.

## Tests

The repository includes lightweight native tests for non-audio modules:

- `SampleWranglerCatalogDbTests`
- `SampleWranglerScannerAppleLoopTests`
- `SampleWranglerWaveformPeakTests`

After building a preset, you can run them with CTest:

```bash
ctest --test-dir build/vs2022-debug -C Debug
```

## Repository Layout

```text
Source/
  App/          Application entry point and top-level UI wiring
  UI/           Browser, results, waveform, and preview panels
  Catalog/      SQLite catalog, schema, models, and waveform cache DB
  Pipeline/     Job queue, scanner, analysis, waveform cache, REX support
  Audio/        Audio engine, voices, and MIDI input routing
  Util/         Paths, hashing, and logging helpers
Tests/          Native tests for catalog, scanner metadata, and waveform peaks
third_party/
  sqlite/       Vendored SQLite amalgamation
  rubberband/   Rubber Band v4 source submodule
JUCE/           JUCE framework submodule
REXSDK_Win_1.9.2/  REX SDK integration assets
docs/           Setup and archive notes
```

## Technology Stack

- C++20
- JUCE
- SQLite with FTS5
- CMake presets for Visual Studio 2022
- Rubber Band v4 for optional high-quality stretch
- Propellerhead REX SDK integration for REX and RX2 support

## Licensing And Third-Party Notes

SampleWrangler itself is distributed under the MIT License. See [LICENSE.txt](LICENSE.txt).

Important third-party considerations:

- JUCE is included as a submodule and remains subject to JUCE's own license terms.
- MP3 support is enabled through `JUCE_USE_MP3AUDIOFORMAT=1`; JUCE documents a legal disclaimer for MP3 support that you should review before redistribution.
- Rubber Band v4 is included as a submodule for high-quality stretch. Rubber Band is GPL unless you have a commercial license.
- REX and RX2 support depends on the Propellerhead REX SDK and its license terms.

## Current Status

This repository already contains a functioning Windows-focused MVP with scanning, search, waveform caching, preview playback, MIDI preview routing, ASIO-capable device management, and packaging support. The codebase is organized to keep UI, database, worker-thread processing, and the real-time audio path separated.

For more setup detail, see [docs/DEVSETUP.md](docs/DEVSETUP.md).
