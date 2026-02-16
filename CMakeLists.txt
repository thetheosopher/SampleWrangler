cmake_minimum_required(VERSION 3.15)
project(SampleWrangler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE (submodule in /JUCE)
# ---------------------------------------------------------------------------
add_subdirectory(JUCE)

# ---------------------------------------------------------------------------
# SQLite amalgamation (vendored in third_party/sqlite)
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC
    third_party/sqlite/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
)
# Suppress MSVC warnings for third-party C code
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(APP_SOURCES
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/MainComponent.h
)

set(UI_SOURCES
    Source/UI/BrowserPanel.cpp
    Source/UI/BrowserPanel.h
    Source/UI/ResultsPanel.cpp
    Source/UI/ResultsPanel.h
    Source/UI/WaveformPanel.cpp
    Source/UI/WaveformPanel.h
    Source/UI/PreviewPanel.cpp
    Source/UI/PreviewPanel.h
)

set(CATALOG_SOURCES
    Source/Catalog/CatalogDb.cpp
    Source/Catalog/CatalogDb.h
    Source/Catalog/CatalogSchema.cpp
    Source/Catalog/CatalogSchema.h
    Source/Catalog/CatalogModels.h
)

set(PIPELINE_SOURCES
    Source/Pipeline/JobQueue.cpp
    Source/Pipeline/JobQueue.h
    Source/Pipeline/Scanner.cpp
    Source/Pipeline/Scanner.h
    Source/Pipeline/Analyzer.cpp
    Source/Pipeline/Analyzer.h
    Source/Pipeline/WaveformCache.cpp
    Source/Pipeline/WaveformCache.h
)

set(AUDIO_SOURCES
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/VoiceManager.cpp
    Source/Audio/VoiceManager.h
    Source/Audio/MidiInputRouter.cpp
    Source/Audio/MidiInputRouter.h
)

set(UTIL_SOURCES
    Source/Util/Paths.cpp
    Source/Util/Paths.h
    Source/Util/Hashing.cpp
    Source/Util/Hashing.h
    Source/Util/Logging.h
)

# ---------------------------------------------------------------------------
# JUCE application target
# ---------------------------------------------------------------------------
juce_add_gui_app(SampleWrangler
    PRODUCT_NAME "SampleWrangler"
    VERSION "0.1.0"
    COMPANY_NAME "SampleWrangler"
)

juce_generate_juce_header(SampleWrangler)

target_sources(SampleWrangler PRIVATE
    ${APP_SOURCES}
    ${UI_SOURCES}
    ${CATALOG_SOURCES}
    ${PIPELINE_SOURCES}
    ${AUDIO_SOURCES}
    ${UTIL_SOURCES}
)

target_include_directories(SampleWrangler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(SampleWrangler PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Enable MP3 reading support — note JUCE legal disclaimer applies.
    # See: https://docs.juce.com/develop/classjuce_1_1MP3AudioFormat.html
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SampleWrangler,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SampleWrangler,JUCE_VERSION>"
)

# ---------------------------------------------------------------------------
# Link JUCE modules + SQLite
# ---------------------------------------------------------------------------
target_link_libraries(SampleWrangler PRIVATE
    juce::juce_core
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    sqlite3
)
