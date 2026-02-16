---
applyTo: "**/CMakeLists.txt,**/CMakePresets.json,**/*.cmake"
---

# CMake Instructions (JUCE / SampleWrangler)

- Use CMake >= 3.15. [3](https://ccrma.stanford.edu/~jos/juce_modules/md__Users_jos_w_JUCEModulesDoc_docs_CMake_API.html)
- Use MSVC toolchain on Windows; do not add MinGW instructions. [4](https://forum.juce.com/t/cmake-windows/64753)[3](https://ccrma.stanford.edu/~jos/juce_modules/md__Users_jos_w_JUCEModulesDoc_docs_CMake_API.html)
- Prefer JUCE as a subdirectory (`add_subdirectory(JUCE)`) or FetchContent if needed. [3](https://ccrma.stanford.edu/~jos/juce_modules/md__Users_jos_w_JUCEModulesDoc_docs_CMake_API.html)
- Keep build options explicit (Debug/Release), and define JUCE flags via `target_compile_definitions`.
- Document any flags for MP3 support clearly (JUCE_USE_MP3AUDIOFORMAT if used). [5](https://docs.juce.com/develop/classjuce_1_1MP3AudioFormat.html)
- Ensure the SampleWrangler app target links required JUCE modules: core, gui_basics, gui_extra, audio_devices, audio_formats, audio_utils, dsp (as needed).