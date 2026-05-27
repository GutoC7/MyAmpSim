#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }