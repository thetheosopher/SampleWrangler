cmake_minimum_required(VERSION 3.15)
project(SampleWrangler VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
if(MSVC)
    set_property(TARGET sqlite3 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(sqlite3 PRIVATE
        $<$<CONFIG:Debug>:/MTd>
        $<$<NOT:$<CONFIG:Debug>>:/MT>
    )
endif()
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
    Source/App/SampleWranglerIcon.rc
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
    VERSION "0.9.0"
    COMPANY_NAME "SampleWrangler"
)

juce_add_binary_data(SampleWranglerAssets
    SOURCES
        images/SampleWrangler.ico
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
# Rubber Band v4 integration (GPL) - Single-file build from source
# ---------------------------------------------------------------------------
set(SW_REQUIRE_RUBBERBAND OFF CACHE BOOL "Require Rubber Band v4 at configure time")
set(RUBBERBAND_SOURCE_DIR "" CACHE PATH "Path to Rubber Band v4 source directory (contains single/RubberBandSingle.cpp)")

# Check if Rubber Band source directory is provided and valid
set(SW_HAVE_RUBBERBAND 0)
set(RUBBERBAND_SOURCE_PATH "")

if(RUBBERBAND_SOURCE_DIR)
    set(RUBBERBAND_SOURCE_PATH "${RUBBERBAND_SOURCE_DIR}")
elseif(DEFINED ENV{RUBBERBAND_SOURCE_DIR})
    set(RUBBERBAND_SOURCE_PATH "$ENV{RUBBERBAND_SOURCE_DIR}")
endif()

# Rubber Band uses single-file build: single/RubberBandSingle.cpp
if(RUBBERBAND_SOURCE_PATH AND EXISTS "${RUBBERBAND_SOURCE_PATH}/single/RubberBandSingle.cpp")
    message(STATUS "Building Rubber Band HQ from source (GPL): ${RUBBERBAND_SOURCE_PATH}")

    # Create a static library from the single-file build
    add_library(rubberband STATIC
        "${RUBBERBAND_SOURCE_PATH}/single/RubberBandSingle.cpp"
    )

    # Rubber Band headers are in the rubberband/ subdirectory
    target_include_directories(rubberband PUBLIC
        "${RUBBERBAND_SOURCE_PATH}"
    )

    # Prevent Windows min/max macro conflicts
    target_compile_definitions(rubberband PRIVATE NOMINMAX)

    # Suppress warnings from third-party code
    if(MSVC)
        target_compile_options(rubberband PRIVATE /W0)
    else()
        target_compile_options(rubberband PRIVATE -w)
    endif()

    set(SW_HAVE_RUBBERBAND 1)
else()
    message(STATUS "Rubber Band HQ not found; Stretch HQ will be unavailable")
    message(STATUS "  Set RUBBERBAND_SOURCE_DIR (or env RUBBERBAND_SOURCE_DIR) to Rubber Band v4 source directory")
endif()

if(SW_REQUIRE_RUBBERBAND AND NOT SW_HAVE_RUBBERBAND)
    message(FATAL_ERROR
        "Rubber Band v4 is required but was not found. "
        "Set RUBBERBAND_SOURCE_DIR (or env RUBBERBAND_SOURCE_DIR) to the Rubber Band v4 source directory containing single/RubberBandSingle.cpp.")
endif()

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(SampleWrangler PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Enable MP3 reading support — note JUCE legal disclaimer applies.
    # See: https://docs.juce.com/develop/classjuce_1_1MP3AudioFormat.html
    JUCE_USE_MP3AUDIOFORMAT=1
    SW_HAVE_RUBBERBAND=${SW_HAVE_RUBBERBAND}
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
    SampleWranglerAssets
    sqlite3
)

if(MSVC)
    target_link_options(SampleWrangler PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
endif()

if(SW_HAVE_RUBBERBAND)
    target_link_libraries(SampleWrangler PRIVATE rubberband)
endif()

include(GNUInstallDirs)
install(TARGETS SampleWrangler
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------------------------------------------------------------------------
# Packaging (CPack)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "SampleWrangler")
set(CPACK_PACKAGE_VENDOR "SampleWrangler")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SampleWrangler")
set(CPACK_PACKAGE_FILE_NAME "SampleWrangler-${PROJECT_VERSION}-win64")
set(CPACK_MONOLITHIC_INSTALL ON)

if(WIN32)
    set(CPACK_GENERATOR "WIX;ZIP")
    set(CPACK_WIX_UPGRADE_GUID "E795F1BB-4E44-4F6C-BAE6-4371EA683986")
    set(CPACK_WIX_UI_REF "WixUI_FeatureTree")
    set(CPACK_WIX_PROPERTY_ARPSYSTEMCOMPONENT "0")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/images/SampleWrangler.ico")
    set(CPACK_WIX_EXTRA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SampleWranglerShortcuts.wxs")
endif()

include(CPack)

# ---------------------------------------------------------------------------
# Tests (lightweight non-audio modules)
# ---------------------------------------------------------------------------
enable_testing()

add_executable(SampleWranglerCatalogDbTests
    Tests/CatalogDbTests.cpp
    Source/Catalog/CatalogDb.cpp
    Source/Catalog/CatalogSchema.cpp
)

target_include_directories(SampleWranglerCatalogDbTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(SampleWranglerCatalogDbTests PRIVATE
    sqlite3
)

add_test(NAME SampleWrangler.CatalogDb COMMAND SampleWranglerCatalogDbTests)

add_executable(SampleWranglerScannerAppleLoopTests
    Tests/ScannerAppleLoopTests.cpp
    Source/Pipeline/Scanner.cpp
    Source/Pipeline/JobQueue.cpp
    Source/Catalog/CatalogDb.cpp
    Source/Catalog/CatalogSchema.cpp
)

target_include_directories(SampleWranglerScannerAppleLoopTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(SampleWranglerScannerAppleLoopTests PRIVATE
    sqlite3
    juce::juce_core
    juce::juce_audio_formats
)

if(MSVC)
    target_link_options(SampleWranglerCatalogDbTests PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
    target_link_options(SampleWranglerScannerAppleLoopTests PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
endif()

add_test(NAME SampleWrangler.ScannerAppleLoop COMMAND SampleWranglerScannerAppleLoopTests)
