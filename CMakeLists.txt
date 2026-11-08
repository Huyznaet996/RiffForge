cmake_minimum_required(VERSION 3.22)

project(RiffForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

option(RIFFFORGE_COPY_AFTER_BUILD "Install the plugin into the system plugin folders after building" OFF)
option(RIFFFORGE_BUILD_TESTS "Build the offline render test" ON)
set(RIFFFORGE_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (Windows only, enables ASIO in the standalone app)")

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if one is given, otherwise download it.
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(RIFFFORGE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RIFFFORGE_FORMATS AU)
elseif(UNIX)
    list(APPEND RIFFFORGE_FORMATS LV2)
endif()

juce_add_plugin(RiffForge
    COMPANY_NAME                  "RiffForge"
    BUNDLE_ID                     "com.riffforge.amp"
    PRODUCT_NAME                  "RiffForge"
    PLUGIN_MANUFACTURER_CODE      Rfrg
    PLUGIN_CODE                   Rfa1
    FORMATS                       ${RIFFFORGE_FORMATS}
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE
    VST3_CATEGORIES               Fx Distortion
    AU_MAIN_TYPE                  kAudioUnitType_Effect
    LV2URI                        "urn:riffforge:guitar-amp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "RiffForge needs the audio input to hear your guitar."
    COPY_PLUGIN_AFTER_BUILD       ${RIFFFORGE_COPY_AFTER_BUILD})

set(RIFFFORGE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/gui/LookAndFeel.cpp)

target_sources(RiffForge PRIVATE ${RIFFFORGE_SOURCES})
target_include_directories(RiffForge PRIVATE Source)

set(RIFFFORGE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(WIN32 AND RIFFFORGE_ASIO_SDK_DIR)
    message(STATUS "RiffForge: ASIO enabled, SDK at ${RIFFFORGE_ASIO_SDK_DIR}")
    list(APPEND RIFFFORGE_DEFINITIONS JUCE_ASIO=1)
    target_include_directories(RiffForge PRIVATE "${RIFFFORGE_ASIO_SDK_DIR}/common")
endif()

target_compile_definitions(RiffForge PUBLIC ${RIFFFORGE_DEFINITIONS})

set(RIFFFORGE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp)

target_link_libraries(RiffForge
    PRIVATE
        ${RIFFFORGE_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: renders a synthetic guitar through every preset into WAV files,
# checks the output for NaN/Inf/silence and saves a screenshot of the editor.
# ---------------------------------------------------------------------------
if(RIFFFORGE_BUILD_TESTS)
    juce_add_console_app(RiffForgeRenderTest PRODUCT_NAME "RiffForgeRenderTest")
    target_sources(RiffForgeRenderTest PRIVATE tests/RenderTest.cpp ${RIFFFORGE_SOURCES})
    target_include_directories(RiffForgeRenderTest PRIVATE Source)
    target_compile_definitions(RiffForgeRenderTest PRIVATE
        ${RIFFFORGE_DEFINITIONS}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(RiffForgeRenderTest
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # Loads the built plugin binaries like a DAW would (VST3/LV2/AU hosting).
    juce_add_console_app(RiffForgeHostTest PRODUCT_NAME "RiffForgeHostTest")
    target_sources(RiffForgeHostTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(RiffForgeHostTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=$<BOOL:${UNIX}>
        JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>)
    target_link_libraries(RiffForgeHostTest
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
