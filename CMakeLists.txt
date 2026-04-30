cmake_minimum_required(VERSION 3.15)

project(MyAmpSim VERSION 0.0.1)

add_subdirectory(JUCE)

juce_add_plugin(MyAmpSim
    COMPANY_NAME "MyName"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "MyNm"
    PLUGIN_CODE "Amps"
    FORMATS VST3 Standalone
    PRODUCT_NAME "My Amp Sim")

# link C++ file
target_sources(MyAmpSim PRIVATE
    Source/PluginProcessor.cpp)

# link the required JUCE modules
target_link_libraries(MyAmpSim PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_dsp) # required for the Convolution Cab Sim

juce_generate_juce_header(MyAmpSim)