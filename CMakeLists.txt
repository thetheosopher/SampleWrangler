cmake_minimum_required(VERSION 3.15)
project(SampleWrangler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

string(TIMESTAMP SW_BUILD_YEAR "%Y")
set(SW_COMPANY_NAME "Michael A. McCloskey")
set(SW_PRODUCT_NAME "SampleWrangler")
set(SW_COPYRIGHT_TEXT "Copyright (c) ${SW_BUILD_YEAR} ${SW_COMPANY_NAME}")
set(SW_FILE_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")

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
# REX SDK (dynamic loader — compiles REX.c and loads REX Shared Library.dll
# at runtime via LoadLibrary / GetProcAddress)
# ---------------------------------------------------------------------------
set(SW_HAVE_REX 0)
set(REX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/REXSDK_Win_1.9.2")

if(EXISTS "${REX_SDK_DIR}/REX.c" AND EXISTS "${REX_SDK_DIR}/REX.h")
    message(STATUS "REX SDK found: ${REX_SDK_DIR}")

    # Compile REX.c as a small static C library (like sqlite3).
    add_library(rex_loader STATIC "${REX_SDK_DIR}/REX.c")
    target_include_directories(rex_loader PUBLIC "${REX_SDK_DIR}")
    target_compile_definitions(rex_loader PRIVATE
        REX_WINDOWS=1
        REX_MAC=0
        REX_DLL_LOADER=1
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        set_property(TARGET rex_loader PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(rex_loader PRIVATE /W0
            $<$<CONFIG:Debug>:/MTd>
            $<$<NOT:$<CONFIG:Debug>>:/MT>
        )
    endif()
    # Link shlwapi.lib + version.lib — REX.c uses them for path helpers and
    # GetFileVersionInfoSize / VerQueryValue.
    target_link_libraries(rex_loader PRIVATE shlwapi version)

    set(SW_HAVE_REX 1)
else()
    message(STATUS "REX SDK not found at ${REX_SDK_DIR}; REX file support disabled")
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
    Source/Catalog/WaveCacheBlobDb.cpp
    Source/Catalog/WaveCacheBlobDb.h
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
    Source/Pipeline/WaveformPeak.cpp
    Source/Pipeline/WaveformPeak.h
    Source/Pipeline/RexManager.cpp
    Source/Pipeline/RexManager.h
)

set(AUDIO_SOURCES
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/Voice.h
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
    VERSION "1.0.0"
    BUNDLE_ID "com.michaelamccloskey.samplewrangler"
    COMPANY_NAME "${SW_COMPANY_NAME}"
    COMPANY_COPYRIGHT "${SW_COPYRIGHT_TEXT}"
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
    ${CMAKE_CURRENT_BINARY_DIR}
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
    # Enable ASIO audio device support on Windows
    JUCE_ASIO=1
    SW_HAVE_RUBBERBAND=${SW_HAVE_RUBBERBAND}
    SW_HAVE_REX=${SW_HAVE_REX}
    JUCE_USER_DEFINED_RC_FILE="SampleWranglerVersion.rc"
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

if(SW_HAVE_REX)
    target_link_libraries(SampleWrangler PRIVATE rex_loader)
    target_include_directories(SampleWrangler PRIVATE "${REX_SDK_DIR}")
    target_compile_definitions(SampleWrangler PRIVATE
        REX_WINDOWS=1
        REX_MAC=0
        REX_DLL_LOADER=1
    )

    # Copy REX Shared Library DLL next to the built executable so it can
    # be found at runtime by REXInitializeDLL_DirPath().
    set(REX_DLL_DIR_DEBUG   "${REX_SDK_DIR}/Win/x64/Testing")
    set(REX_DLL_DIR_RELEASE "${REX_SDK_DIR}/Win/x64/Deployment")
    set(REX_DLL_SOURCE "$<IF:$<CONFIG:Debug>,${REX_DLL_DIR_DEBUG}/REX Shared Library Testing.dll,${REX_DLL_DIR_RELEASE}/REX Shared Library.dll>")
    add_custom_command(TARGET SampleWrangler POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${REX_DLL_SOURCE}"
            "$<TARGET_FILE_DIR:SampleWrangler>"
        COMMENT "Copying REX Shared Library DLL alongside executable"
        COMMAND_EXPAND_LISTS
    )
endif()

include(GNUInstallDirs)
install(TARGETS SampleWrangler
    RUNTIME DESTINATION .
    COMPONENT Runtime
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION .
    COMPONENT Runtime
)

if(SW_HAVE_REX)
    install(FILES
        "${REX_SDK_DIR}/Win/x64/Deployment/REX Shared Library.dll"
        DESTINATION .
        COMPONENT Runtime
    )
endif()

# ---------------------------------------------------------------------------
# Packaging (Inno Setup + portable ZIP)
# ---------------------------------------------------------------------------
set(SW_PACKAGE_BASENAME "SampleWrangler-${PROJECT_VERSION}-win64")
set(SW_PACKAGE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/packages")
set(SW_INSTALL_STAGE_DIR "${CMAKE_BINARY_DIR}/package-stage/install-root")
set(SW_PORTABLE_STAGE_DIR "${CMAKE_BINARY_DIR}/package-stage/portable-root")
set(SW_PORTABLE_FOLDER_NAME "${SW_PACKAGE_BASENAME}-portable")
set(SW_PORTABLE_ARCHIVE "${SW_PACKAGE_OUTPUT_DIR}/${SW_PORTABLE_FOLDER_NAME}.zip")
set(SW_INNO_OUTPUT_BASENAME "${SW_PACKAGE_BASENAME}-setup")
set(SW_INNO_OUTPUT_FILE "${SW_PACKAGE_OUTPUT_DIR}/${SW_INNO_OUTPUT_BASENAME}.exe")
set(SW_INNO_SETUP_COMPILER "" CACHE FILEPATH "Path to Inno Setup 6 ISCC.exe")

set(SW_INNO_SETUP_COMPILER_RESOLVED "${SW_INNO_SETUP_COMPILER}")
if(WIN32 AND NOT SW_INNO_SETUP_COMPILER_RESOLVED)
    foreach(candidate
        "$ENV{USERPROFILE}/AppData/Local/Programs/Inno Setup 6/ISCC.exe"
        "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6/ISCC.exe"
        "$ENV{USERPROFILE}/scoop/apps/innosetup/current/ISCC.exe"
        "$ENV{USERPROFILE}/AppData/Local/Microsoft/WinGet/Packages/JRSoftware.InnoSetup_*/*/ISCC.exe"
        "$ENV{LOCALAPPDATA}/Inno Setup 6/ISCC.exe"
        "C:/Program Files (x86)/Inno Setup 6/ISCC.exe"
        "C:/Program Files/Inno Setup 6/ISCC.exe")
        if(EXISTS "${candidate}")
            set(SW_INNO_SETUP_COMPILER_RESOLVED "${candidate}")
            break()
        endif()
    endforeach()

    if(NOT SW_INNO_SETUP_COMPILER_RESOLVED)
        file(GLOB SW_INNO_WINGET_CANDIDATES LIST_DIRECTORIES false
            "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Packages/JRSoftware.InnoSetup_*/*/ISCC.exe"
            "$ENV{USERPROFILE}/AppData/Local/Microsoft/WinGet/Packages/JRSoftware.InnoSetup_*/*/ISCC.exe")

        list(SORT SW_INNO_WINGET_CANDIDATES)
        list(REVERSE SW_INNO_WINGET_CANDIDATES)

        foreach(candidate IN LISTS SW_INNO_WINGET_CANDIDATES)
            if(EXISTS "${candidate}")
                set(SW_INNO_SETUP_COMPILER_RESOLVED "${candidate}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(SW_INNO_SETUP_COMPILER_RESOLVED)
    message(STATUS "Inno Setup 6 compiler found: ${SW_INNO_SETUP_COMPILER_RESOLVED}")
else()
    message(STATUS "Inno Setup 6 compiler not found; set SW_INNO_SETUP_COMPILER to build the installer artifact")
endif()

set(SW_INNO_SETUP_COMPILER_CMAKE_PATH "")
if(SW_INNO_SETUP_COMPILER_RESOLVED)
    file(TO_CMAKE_PATH "${SW_INNO_SETUP_COMPILER_RESOLVED}" SW_INNO_SETUP_COMPILER_CMAKE_PATH)
endif()

file(TO_NATIVE_PATH "${SW_INSTALL_STAGE_DIR}" SW_INSTALL_STAGE_DIR_NATIVE)
file(TO_NATIVE_PATH "${SW_PACKAGE_OUTPUT_DIR}" SW_PACKAGE_OUTPUT_DIR_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/images/SampleWrangler.ico" SW_SETUP_ICON_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" SW_LICENSE_FILE_NATIVE)
set(SW_APP_ICON_RC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/images/SampleWrangler.ico")
set(SW_APP_PUBLISHER "${SW_COMPANY_NAME}")
set(SW_APP_COPYRIGHT "${SW_COPYRIGHT_TEXT}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SampleWranglerInstaller.iss.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SampleWranglerInstaller.iss"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInnoSetup.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RunInnoSetup.cmake"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CreatePortableZip.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CreatePortableZip.cmake"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SampleWranglerVersion.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SampleWranglerVersion.rc"
    @ONLY
)

add_custom_target(SampleWranglerStageRelease
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SW_INSTALL_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SW_INSTALL_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config Release --target SampleWrangler
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config Release --component Runtime --prefix "${SW_INSTALL_STAGE_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    COMMENT "Staging release files for packaging"
    VERBATIM
)

add_custom_target(SampleWranglerPortableZip
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/CreatePortableZip.cmake"
    DEPENDS SampleWranglerStageRelease
            "${CMAKE_CURRENT_BINARY_DIR}/CreatePortableZip.cmake"
    COMMENT "Creating portable ZIP artifact"
    VERBATIM
)

add_custom_target(SampleWranglerInnoSetup
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/RunInnoSetup.cmake"
    DEPENDS SampleWranglerStageRelease
            "${CMAKE_CURRENT_BINARY_DIR}/SampleWranglerInstaller.iss"
            "${CMAKE_CURRENT_BINARY_DIR}/RunInnoSetup.cmake"
    COMMENT "Building Inno Setup installer artifact"
    VERBATIM
)

add_custom_target(SampleWranglerReleaseArtifacts
    DEPENDS SampleWranglerInnoSetup SampleWranglerPortableZip
)

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
    Source/Pipeline/RexManager.cpp
    Source/Catalog/CatalogDb.cpp
    Source/Catalog/WaveCacheBlobDb.cpp
    Source/Catalog/CatalogSchema.cpp
)

add_executable(SampleWranglerWaveformPeakTests
    Tests/WaveformPeakTests.cpp
    Source/Pipeline/WaveformPeak.cpp
)

target_include_directories(SampleWranglerWaveformPeakTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(SampleWranglerWaveformPeakTests PRIVATE
    juce::juce_audio_basics
)

target_include_directories(SampleWranglerScannerAppleLoopTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_link_libraries(SampleWranglerScannerAppleLoopTests PRIVATE
    sqlite3
    juce::juce_core
    juce::juce_audio_formats
)

target_compile_definitions(SampleWranglerScannerAppleLoopTests PRIVATE
    SW_HAVE_REX=${SW_HAVE_REX}
)

if(SW_HAVE_REX)
    target_link_libraries(SampleWranglerScannerAppleLoopTests PRIVATE rex_loader)
    target_include_directories(SampleWranglerScannerAppleLoopTests PRIVATE "${REX_SDK_DIR}")
    target_compile_definitions(SampleWranglerScannerAppleLoopTests PRIVATE
        REX_WINDOWS=1
        REX_MAC=0
        REX_DLL_LOADER=1
    )
endif()

if(MSVC)
    target_link_options(SampleWranglerCatalogDbTests PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
    target_link_options(SampleWranglerScannerAppleLoopTests PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
    target_link_options(SampleWranglerWaveformPeakTests PRIVATE
        /NODEFAULTLIB:MSVCRT
        /NODEFAULTLIB:MSVCRTD
    )
endif()

add_test(NAME SampleWrangler.ScannerAppleLoop COMMAND SampleWranglerScannerAppleLoopTests)
add_test(NAME SampleWrangler.WaveformPeak COMMAND SampleWranglerWaveformPeakTests)
