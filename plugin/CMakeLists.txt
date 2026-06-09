cmake_minimum_required(VERSION 3.22)
project(TEKKPreamp VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- get JUCE -------------------------------------------------------------
# Option A (default): fetch a pinned JUCE. Option B: set JUCE_DIR to a local
# checkout and replace this block with add_subdirectory(${JUCE_DIR} juce).
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4          # pin a known-good tag
)
FetchContent_MakeAvailable(juce)

# --- plugin target --------------------------------------------------------
juce_add_plugin(TEKKPreamp
    PRODUCT_NAME            "TEKK Preamp"
    COMPANY_NAME            "TEKK"
    PLUGIN_MANUFACTURER_CODE Tekk
    PLUGIN_CODE             Tpre
    FORMATS                 VST3 AU Standalone
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(TEKKPreamp PRIVATE
    src/PluginProcessor.cpp)

target_include_directories(TEKKPreamp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../dsp)   # shared JUCE-free DSP headers

target_compile_definitions(TEKKPreamp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TEKKPreamp PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_audio_processors
    PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
