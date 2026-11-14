cmake_minimum_required(VERSION 3.22)

project(HYPERCOLOR VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE submodule is missing. Run: git submodule update --init --recursive")
endif()

add_subdirectory(JUCE)

juce_add_plugin(HYPERCOLOR
    COMPANY_NAME "strnbeats"
    BUNDLE_ID "com.strnbeats.hypercolor"

    PLUGIN_MANUFACTURER_CODE Strn
    PLUGIN_CODE HyCl

    PRODUCT_NAME "HYPERCOLOR"

    FORMATS VST3

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE

    VST3_CATEGORIES Fx
)

juce_generate_juce_header(HYPERCOLOR)

target_sources(HYPERCOLOR
    PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
)

target_include_directories(HYPERCOLOR
    PRIVATE
        src/plugin
)

target_compile_features(HYPERCOLOR PRIVATE cxx_std_20)

target_compile_definitions(HYPERCOLOR
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(HYPERCOLOR
    PRIVATE
        juce::juce_audio_utils

    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
