# DEV SETUP — SampleWrangler (JUCE, Windows)

## Prerequisites
- **Visual Studio 2022** (Build Tools or full IDE) with the **Desktop C++** workload (MSVC toolchain)
- **CMake 3.15+** (ships with VS 2022, or install separately)
- **Git**
- **VS Code** + recommended extensions (see `.vscode/extensions.json`)

## Clone & initialise

```bash
git clone <repo-url> SampleWrangler
cd SampleWrangler
git submodule update --init --recursive   # pulls JUCE
```

## Configure & build (CMake Presets)

### Debug

```bash
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
```

### Release

```bash
cmake --preset vs2022-release
cmake --build --preset vs2022-release
```

### Preset cache recovery (VS instance changed)

If you see an error like "could not find specified instance of Visual Studio", the build cache is pinned to an old VS installation. Reset it with:

```bash
rm -r build/vs2022-debug
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
```

PowerShell equivalent:

```powershell
Remove-Item -Recurse -Force "build/vs2022-debug"
cmake --preset vs2022-debug
cmake --build --preset vs2022-debug
```

The executable is written to:

```
build/vs2022-debug/SampleWrangler_artefacts/Debug/SampleWrangler.exe      (Debug)
build/vs2022-release/SampleWrangler_artefacts/Release/SampleWrangler.exe  (Release)
```

## Run

```bash
.\build\vs2022-debug\SampleWrangler_artefacts\Debug\SampleWrangler.exe
```

Or press **F5** in VS Code (the launch config is pre-configured).

## MP3 support

MP3 reading is enabled via `JUCE_USE_MP3AUDIOFORMAT=1` in CMakeLists.txt.
See the [JUCE MP3 legal disclaimer](https://docs.juce.com/develop/classjuce_1_1MP3AudioFormat.html) for details.

## Project layout

```
Source/
  App/          Main entry point + MainComponent
  UI/           Panels: Browser, Results, Waveform, Preview
  Catalog/      SQLite DB, schema, data models
  Pipeline/     JobQueue, Scanner, Analyzer, WaveformCache
  Audio/        AudioEngine, VoiceManager, MidiInputRouter
  Util/         Paths, Hashing, Logging
third_party/
  sqlite/       Vendored SQLite amalgamation
JUCE/           JUCE framework (git submodule)
```

## ASIO

JUCE's `AudioDeviceManager` will list ASIO drivers automatically when available.
No additional SDK configuration is needed for the default JUCE ASIO support on Windows.
