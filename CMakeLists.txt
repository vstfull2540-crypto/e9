cmake_minimum_required(VERSION 3.22)
project(AiVocalEQ VERSION 1.0.0)

# เปิดเมื่อบิลด์บนเครื่องตัวเอง (คัดลอก .vst3 ไปโฟลเดอร์ระบบ) / ปิดบน GitHub Actions
option(COPY_AFTER_BUILD "Copy plugin to system VST3 folder after build" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(AiVocalEQ
    COMPANY_NAME "Arm Nongmuang Studio"
    PLUGIN_MANUFACTURER_CODE Armn
    PLUGIN_CODE Avqe
    FORMATS VST3 Standalone
    PRODUCT_NAME "EQ AI Mix Vocals Full"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

juce_add_binary_data(AiVocalEQAssets SOURCES knob.png)

target_sources(AiVocalEQ PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_features(AiVocalEQ PRIVATE cxx_std_17)

target_compile_definitions(AiVocalEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_options(AiVocalEQ PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(AiVocalEQ
    PRIVATE
        AiVocalEQAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
