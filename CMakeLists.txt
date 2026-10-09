cmake_minimum_required(VERSION 3.20)
project(SSL5BandCompressor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Подтягиваем JUCE 7
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SSL5BandCompressor
    COMPANY_NAME "FlowMusic"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    FORMATS VST3
    PRODUCT_NAME "SSL 5-Band Compressor"
)

target_sources(SSL5BandCompressor PRIVATE
    src/PluginProcessor.h
    src/PluginProcessor.cpp
)

target_compile_definitions(SSL5BandCompressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SSL5BandCompressor PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)