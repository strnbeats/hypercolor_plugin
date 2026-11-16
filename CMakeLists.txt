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

    FORMATS VST3 Standalone

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE

    VST3_CATEGORIES Fx
)

if(APPLE)
    add_custom_command(
        TARGET HYPERCOLOR_Standalone
        POST_BUILD
        COMMAND codesign
            --force
            --deep
            --sign -
            "$<TARGET_BUNDLE_DIR:HYPERCOLOR_Standalone>"
        COMMENT "Applying ad-hoc signature to HYPERCOLOR Standalone"
        VERBATIM
    )
endif()

juce_generate_juce_header(HYPERCOLOR)

target_sources(HYPERCOLOR
    PRIVATE
        src/dsp/granular/CircularAudioBuffer.cpp
        src/dsp/granular/GranularCharacter.cpp
        src/dsp/granular/GranularEngine.cpp
        src/dsp/granular/GranularRandom.cpp
        src/dsp/granular/Grain.cpp
        src/dsp/granular/GrainPool.cpp
        src/dsp/granular/GrainRenderer.cpp
        src/dsp/granular/GrainScheduler.cpp
        src/plugin/GranularVisualizer.cpp
        src/plugin/HypercolorLookAndFeel.cpp
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
)

target_include_directories(HYPERCOLOR
    PRIVATE
        src
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

include(CTest)

if(BUILD_TESTING)
    add_executable(HYPERCOLORTests)

    target_sources(HYPERCOLORTests
        PRIVATE
            tests/ProcessorTests.cpp
    )

    target_include_directories(HYPERCOLORTests
        PRIVATE
            src
            src/plugin
            ${CMAKE_CURRENT_BINARY_DIR}/HYPERCOLOR_artefacts/JuceLibraryCode
            JUCE/modules
    )

    target_link_libraries(HYPERCOLORTests
        PRIVATE
            HYPERCOLOR
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME HYPERCOLOR.Processor COMMAND HYPERCOLORTests)

    add_executable(HYPERCOLORCircularAudioBufferTests
        src/dsp/granular/CircularAudioBuffer.cpp
        tests/CircularAudioBufferTests.cpp
    )

    target_include_directories(HYPERCOLORCircularAudioBufferTests
        PRIVATE
            src
    )

    target_compile_features(
        HYPERCOLORCircularAudioBufferTests
        PRIVATE
            cxx_std_20
    )

    target_link_libraries(HYPERCOLORCircularAudioBufferTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.CircularAudioBuffer
        COMMAND HYPERCOLORCircularAudioBufferTests
    )

    add_executable(HYPERCOLORGrainTests
        src/dsp/granular/CircularAudioBuffer.cpp
        src/dsp/granular/Grain.cpp
        tests/GrainTests.cpp
    )

    target_include_directories(HYPERCOLORGrainTests
        PRIVATE
            src
    )

    target_compile_features(HYPERCOLORGrainTests PRIVATE cxx_std_20)

    target_link_libraries(HYPERCOLORGrainTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME HYPERCOLOR.Grain COMMAND HYPERCOLORGrainTests)

    add_executable(HYPERCOLORGrainPoolTests
        src/dsp/granular/CircularAudioBuffer.cpp
        src/dsp/granular/Grain.cpp
        src/dsp/granular/GrainPool.cpp
        tests/GrainPoolTests.cpp
    )

    target_include_directories(HYPERCOLORGrainPoolTests
        PRIVATE
            src
    )

    target_compile_features(HYPERCOLORGrainPoolTests PRIVATE cxx_std_20)

    target_link_libraries(HYPERCOLORGrainPoolTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME HYPERCOLOR.GrainPool COMMAND HYPERCOLORGrainPoolTests)

    add_executable(HYPERCOLORGrainRendererTests
        src/dsp/granular/CircularAudioBuffer.cpp
        src/dsp/granular/Grain.cpp
        src/dsp/granular/GrainPool.cpp
        src/dsp/granular/GrainRenderer.cpp
        tests/GrainRendererTests.cpp
    )

    target_include_directories(HYPERCOLORGrainRendererTests
        PRIVATE
            src
    )

    target_compile_features(HYPERCOLORGrainRendererTests PRIVATE cxx_std_20)

    target_link_libraries(HYPERCOLORGrainRendererTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.GrainRenderer
        COMMAND HYPERCOLORGrainRendererTests
    )

    add_executable(HYPERCOLORGranularEngineTests
        src/dsp/granular/CircularAudioBuffer.cpp
        src/dsp/granular/GranularCharacter.cpp
        src/dsp/granular/GranularEngine.cpp
        src/dsp/granular/GranularRandom.cpp
        src/dsp/granular/Grain.cpp
        src/dsp/granular/GrainPool.cpp
        src/dsp/granular/GrainRenderer.cpp
        src/dsp/granular/GrainScheduler.cpp
        tests/GranularEngineTests.cpp
    )

    target_include_directories(HYPERCOLORGranularEngineTests
        PRIVATE
            src
    )

    target_compile_features(HYPERCOLORGranularEngineTests PRIVATE cxx_std_20)

    target_link_libraries(HYPERCOLORGranularEngineTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.GranularEngine
        COMMAND HYPERCOLORGranularEngineTests
    )

    add_executable(HYPERCOLORGrainSchedulerTests
        src/dsp/granular/CircularAudioBuffer.cpp
        src/dsp/granular/GranularCharacter.cpp
        src/dsp/granular/GranularRandom.cpp
        src/dsp/granular/Grain.cpp
        src/dsp/granular/GrainPool.cpp
        src/dsp/granular/GrainScheduler.cpp
        tests/GrainSchedulerTests.cpp
    )

    target_include_directories(HYPERCOLORGrainSchedulerTests
        PRIVATE
            src
    )

    target_compile_features(HYPERCOLORGrainSchedulerTests PRIVATE cxx_std_20)

    target_link_libraries(HYPERCOLORGrainSchedulerTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.GrainScheduler
        COMMAND HYPERCOLORGrainSchedulerTests
    )

    add_executable(HYPERCOLORGranularTelemetryTests
        tests/GranularTelemetryTests.cpp
    )

    target_include_directories(HYPERCOLORGranularTelemetryTests
        PRIVATE
            src
    )

    target_compile_features(HYPERCOLORGranularTelemetryTests PRIVATE cxx_std_20)

    target_link_libraries(HYPERCOLORGranularTelemetryTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.GranularTelemetry
        COMMAND HYPERCOLORGranularTelemetryTests
    )

    add_executable(HYPERCOLORGranularCharacterTests
        src/dsp/granular/GranularCharacter.cpp
        tests/GranularCharacterTests.cpp
    )

    target_include_directories(HYPERCOLORGranularCharacterTests PRIVATE src)
    target_compile_features(HYPERCOLORGranularCharacterTests PRIVATE cxx_std_20)
    target_link_libraries(HYPERCOLORGranularCharacterTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.GranularCharacter
        COMMAND HYPERCOLORGranularCharacterTests
    )

    add_executable(HYPERCOLORGranularRandomTests
        src/dsp/granular/GranularRandom.cpp
        tests/GranularRandomTests.cpp
    )

    target_include_directories(HYPERCOLORGranularRandomTests PRIVATE src)
    target_compile_features(HYPERCOLORGranularRandomTests PRIVATE cxx_std_20)
    target_link_libraries(HYPERCOLORGranularRandomTests
        PRIVATE
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME HYPERCOLOR.GranularRandom
        COMMAND HYPERCOLORGranularRandomTests
    )
endif()
