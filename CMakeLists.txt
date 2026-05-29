cmake_minimum_required(VERSION 3.15)

project(MyAmpSim VERSION 0.0.1)

add_subdirectory(JUCE)

# --- AUTO-DETECT AND EMBED FACTORY IMPULSE RESPONSES ---
# Use the absolute path to guarantee CMake looks in the correct folder
file(GLOB IR_WAV_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.wav")

if(IR_WAV_FILES)
    juce_add_binary_data(MyAmpSimBinaryData
        HEADER_NAME "BinaryData.h"
        NAMESPACE BinaryData
        SOURCES ${IR_WAV_FILES}
    )
else()
    # Throw a clear error if the folder is empty or missing
    message(FATAL_ERROR "CRITICAL: No .wav files were found in ${CMAKE_CURRENT_SOURCE_DIR}/Assets/")
endif()

juce_add_plugin(MyAmpSim
    COMPANY_NAME "GutoC7"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE "GC7"
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
    juce::juce_dsp 
    #juce::juce::juce_recommended_config_flags
    #juce::juce::juce_recommended_lto_flags
    #juce::juce::juce_recommended_warning_flags
    MyAmpSimBinaryData) 

juce_generate_juce_header(MyAmpSim)