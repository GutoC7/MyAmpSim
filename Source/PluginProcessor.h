#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpMath.h"
#include "Effects.h"
#include "PresetFactory.h"

// --- THE PROCESSOR (Audio Engine) ---
class MyAmpSimAudioProcessor : public juce::AudioProcessor
{
public:
    GuitarTuner tuner;
    std::atomic<float> currentPitchHz{ 0.0f }; // So the GUI can read it
    std::atomic<float> midiPitchHz{ 0.0f };   // Tracks the MIDI keyboard
    std::atomic<float> outputPeak{ 0.0f }; // Tracks the master output volume

    // --- MIDI LEARN SYSTEM ---
    std::atomic<int> lastMovedCC{ -1 }; // Remembers the physical knob you just touched
    std::atomic<bool> isMidiLearnActive{ false }; // Flag controlled by the UI
    juce::RangedAudioParameter* ccBindings[128]{ nullptr }; // Fast lookup array

    // Safely binds a physical CC number to an APVTS parameter ID
    void bindCC(int ccNumber, const juce::String& paramID)
    {
        if (ccNumber >= 0 && ccNumber < 128) {
            ccBindings[ccNumber] = apvts.getParameter(paramID);
        }
    }

    // --- FFT VISUALIZER TUNNEL ---
    enum { fftOrder = 10, fftSize = 1 << fftOrder };

    juce::dsp::FFT forwardFFT{ fftOrder };
    juce::dsp::WindowingFunction<float> window{ fftSize, juce::dsp::WindowingFunction<float>::hann };

    float fifo[fftSize];
    std::atomic<int> fifoIndex{ 0 };
    bool nextFFTBlockReady = false;

    float fftData[fftSize * 2]; // 2x size because FFT calculates complex numbers
    float waveData[fftSize]; // copy for the oscilloscope

    // The Audio Thread calls this thousands of times a second
    void pushNextSampleIntoFifo(float sample)
    {
        // When the tunnel is full...
        if (fifoIndex == fftSize) {
            // ...and the UI has finished drawing the last frame...
            if (!nextFFTBlockReady) {
                // ...push the new block of audio into the public array for the UI to read
                std::fill(fftData, fftData + (fftSize * 2), 0.0f);
                std::copy(fifo, fifo + fftSize, fftData);
                std::copy(fifo, fifo + fftSize, waveData);
                nextFFTBlockReady = true;
            }
            fifoIndex = 0;
        }
        fifo[fifoIndex++] = sample;
    }

    // Safely searches the pedalboard for the Cabinet and loads the IR
    void loadCabinetIR(const juce::File& file)
    {
        for (auto& pedal : pedalboard) {
            if (pedal->getName() == "Cab") {
                // Cast the generic base class back into a CabinetPedal to access its custom function
                if (auto* cab = dynamic_cast<CabinetPedal*>(pedal.get())) {
                    cab->loadImpulseResponse(file);
                }
                break;
            }
        }
    }

    // THE CENTRAL DATABASE
    juce::AudioProcessorValueTreeState apvts;
    juce::File presetDirectory;

    // THE PEDALBOARD
    std::vector<std::unique_ptr<AudioEffect>> pedalboard;

    std::atomic<int> routingMap[20];

    MyAmpSimAudioProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        // Initialize the APVTS with our parameter layout
        apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        // Create a dedicated folder in the user's AppData/Application Support directory
        presetDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("MyAmpSim").getChildFile("Presets");

        if (!presetDirectory.exists()) {
            presetDirectory.createDirectory();
        }

		PresetFactory::installDefaultPresets(presetDirectory); // run the first-time setup to populate the folder with some presets

        // 0-7: Dynamics, Pitch, and Drive (Top Row)
        pedalboard.push_back(std::make_unique<NoiseGatePedal>(apvts.getRawParameterValue("ng_thresh"), apvts.getRawParameterValue("ng_ratio"), apvts.getRawParameterValue("ng_att"), apvts.getRawParameterValue("ng_rel")));
        pedalboard.push_back(std::make_unique<CompressorPedal>(apvts.getRawParameterValue("cmp_thresh"), apvts.getRawParameterValue("cmp_ratio"), apvts.getRawParameterValue("cmp_att"), apvts.getRawParameterValue("cmp_rel")));
        pedalboard.push_back(std::make_unique<BoosterPedal>(apvts.getRawParameterValue("bst_gain")));
        pedalboard.push_back(std::make_unique<DistortionPedal>(apvts.getRawParameterValue("drive"), apvts.getRawParameterValue("dist_type")));
        pedalboard.push_back(std::make_unique<EqPedal>(apvts.getRawParameterValue("eq_low"), apvts.getRawParameterValue("eq_mid"), apvts.getRawParameterValue("eq_high")));
        pedalboard.push_back(std::make_unique<PitchShifterPedal>(apvts.getRawParameterValue("ps_semi"), apvts.getRawParameterValue("ps_mix")));
        pedalboard.push_back(std::make_unique<OctaverPedal>(apvts.getRawParameterValue("oct_semi"), apvts.getRawParameterValue("oct_mix")));
        pedalboard.push_back(std::make_unique<CabinetPedal>());

        // 8-19: Modulation, Time, and Synths (Bottom Row)
        pedalboard.push_back(std::make_unique<AutoWahPedal>(apvts.getRawParameterValue("wah_rate"), apvts.getRawParameterValue("wah_depth"), apvts.getRawParameterValue("wah_q")));
        pedalboard.push_back(std::make_unique<PhaserPedal>(apvts.getRawParameterValue("phs_rate"), apvts.getRawParameterValue("phs_depth"), apvts.getRawParameterValue("phs_freq"), apvts.getRawParameterValue("phs_feed")));
        pedalboard.push_back(std::make_unique<FlangerPedal>(apvts.getRawParameterValue("flg_rate"), apvts.getRawParameterValue("flg_depth"), apvts.getRawParameterValue("flg_feed")));
        pedalboard.push_back(std::make_unique<TremoloPedal>(apvts.getRawParameterValue("trem_depth"), apvts.getRawParameterValue("trem_rate")));
        pedalboard.push_back(std::make_unique<ChorusPedal>(apvts.getRawParameterValue("cho_rate"), apvts.getRawParameterValue("cho_depth"), apvts.getRawParameterValue("cho_mix")));
        pedalboard.push_back(std::make_unique<DelayPedal>(apvts.getRawParameterValue("delay_time"), apvts.getRawParameterValue("delay_feed"), apvts.getRawParameterValue("delay_mix")));
        pedalboard.push_back(std::make_unique<ReverbPedal>(apvts.getRawParameterValue("rvb_room"), apvts.getRawParameterValue("rvb_damp"), apvts.getRawParameterValue("rvb_mix")));
        pedalboard.push_back(std::make_unique<AcousticSimPedal>(apvts.getRawParameterValue("ac_body"), apvts.getRawParameterValue("ac_air"), apvts.getRawParameterValue("ac_reso")));
        pedalboard.push_back(std::make_unique<GuitarSynthPedal>(apvts.getRawParameterValue("syn_type"), apvts.getRawParameterValue("syn_mix"), &currentPitchHz, &midiPitchHz));
        pedalboard.push_back(std::make_unique<LooperPedal>(apvts.getRawParameterValue("loop_state"), apvts.getRawParameterValue("loop_level")));
        pedalboard.push_back(std::make_unique<BitcrusherPedal>(apvts.getRawParameterValue("bc_bits"), apvts.getRawParameterValue("bc_down")));
        pedalboard.push_back(std::make_unique<RingModPedal>(apvts.getRawParameterValue("rm_freq"), apvts.getRawParameterValue("rm_mix")));

        // Setup Routing mapping and FORCE default state to Bypassed (Off)
        for (int i = 0; i < 20; ++i) {
            routingMap[i].store(i); // Safely initialize the map

            // Only attempt to bind the bypass switch if the pedal actually exists
            if (i < pedalboard.size()) {
                juce::String bypassID = "byp_" + juce::String(i);
                pedalboard[i]->isBypassed = apvts.getRawParameterValue(bypassID);

                if (auto* param = apvts.getParameter(bypassID)) {
                    param->setValueNotifyingHost(1.0f);
                }
            }
        }
    }

    ~MyAmpSimAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        tuner.prepare(sampleRate);

        // Prepare every pedal in the chain
        for (auto& pedal : pedalboard)
        {
            pedal->prepare(sampleRate, samplesPerBlock);
        }
    }

    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        juce::ScopedNoDenormals noDenormals;

        // read MIDI keyboard events
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                midiPitchHz.store((float)juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()));
            }
            else if (msg.isNoteOff()) {
                if (std::abs((float)juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()) - midiPitchHz.load()) < 1.0f) {
                    midiPitchHz.store(0.0f);
                }
            }
            // ---> ADD THIS NEW BLOCK: Catch hardware knobs & expression pedals <---
            else if (msg.isController()) {
                int cc = msg.getControllerNumber();
                float val = msg.getControllerValue() / 127.0f; // Normalize 0-127 into 0.0-1.0

                // 1. Remember this CC in case the user is trying to "Learn" a new mapping
                lastMovedCC.store(cc);

                // 2. If it's already mapped, physically turn the parameter under the hood!
                if (ccBindings[cc] != nullptr) {
                    ccBindings[cc]->setValueNotifyingHost(val);
                }
            }
        }

        // Clear garbage memory
        for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        {
            buffer.clear(i, 0, buffer.getNumSamples());
        }

        if (getTotalNumInputChannels() > 1)
            buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples()); // Ensure stereo if mono input

        auto* readPointer = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            tuner.process(readPointer[i]);
        }
        currentPitchHz.store(tuner.getHz()); // Update the shared variable for the GUI to read


        // PROCESS AUDIO USING THE DYNAMIC ROUTING MAP
        for (int i = 0; i < 20; ++i)
        {
            int pedalIndex = routingMap[i].load();

            // Never access the vector unless 100% sure the pedal exists
            if (pedalIndex >= 0 && pedalIndex < pedalboard.size())
            {
                pedalboard[pedalIndex]->process(buffer);

                // Capture the audio immediately after the EQ pedal processes it
                if (pedalboard[pedalIndex]->getName() == "EQ") {
                    auto* readPtr = buffer.getReadPointer(0); // We only need the Left channel for visuals
                    for (int s = 0; s < buffer.getNumSamples(); ++s) {
                        pushNextSampleIntoFifo(readPtr[s]);
                    }
                }
            }
        }

        buffer.applyGain(apvts.getRawParameterValue("master_vol")->load());

        // Calculate the maximum volume of this block and send it to the UI
        float currentPeak = buffer.getMagnitude(0, buffer.getNumSamples());
        outputPeak.store(currentPeak);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "My Amp Sim"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}
    void getStateInformation(juce::MemoryBlock& destData) override
    {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());

        // NUCLEAR OPTION: Delete any routing tags that sneaked into the XML
        xml->deleteAllChildElementsWithTagName("ROUTING");

        auto* routingXml = new juce::XmlElement("ROUTING");
        for (int i = 0; i < 20; ++i) routingXml->setAttribute("slot" + juce::String(i), routingMap[i].load());

        xml->addChildElement(routingXml);
        copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
        if (xmlState != nullptr)
        {
            // 1. Find the LAST routing tag (ignores the old corrupted parasite tags)
            juce::XmlElement* bestRouting = nullptr;
            for (auto* child : xmlState->getChildIterator()) {
                if (child->hasTagName("ROUTING")) bestRouting = child;
            }

            if (bestRouting != nullptr) {
                bool isPedalUsed[20] = { false };

                // 2. Try to load the slots safely
                for (int slot = 0; slot < 20; ++slot) {
                    int pedalID = bestRouting->getIntAttribute("slot" + juce::String(slot), slot);
                    if (pedalID >= 0 && pedalID < 20 && !isPedalUsed[pedalID]) {
                        routingMap[slot].store(pedalID);
                        isPedalUsed[pedalID] = true;
                    }
                    else {
                        routingMap[slot].store(-1); // Mark slot as corrupted/empty
                    }
                }

                // 3. Heal any corrupted/missing slots
                for (int slot = 0; slot < 20; ++slot) {
                    if (routingMap[slot].load() == -1) {
                        for (int p = 0; p < 20; ++p) { // Find a pedal not yet on the board
                            if (!isPedalUsed[p]) {
                                routingMap[slot].store(p);
                                isPedalUsed[p] = true;
                                break;
                            }
                        }
                    }
                }
            }

            // 4. Scrub the XML and feed it to the APVTS safely
            xmlState->deleteAllChildElementsWithTagName("ROUTING");
            if (xmlState->hasTagName(apvts.state.getType())) {
                apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
            }
        }
    }

    void loadPresetSilently(const juce::File& presetFile)
    {
        if (!presetFile.existsAsFile()) return;

        std::unique_ptr<juce::XmlElement> xmlState = juce::XmlDocument::parse(presetFile);
        if (xmlState != nullptr)
        {
            juce::XmlElement* bestRouting = nullptr;
            for (auto* child : xmlState->getChildIterator()) {
                if (child->hasTagName("ROUTING")) bestRouting = child;
            }

            if (bestRouting != nullptr) {
                bool isPedalUsed[20] = { false };
                for (int slot = 0; slot < 20; ++slot) {
                    int pedalID = bestRouting->getIntAttribute("slot" + juce::String(slot), slot);
                    if (pedalID >= 0 && pedalID < 20 && !isPedalUsed[pedalID]) {
                        routingMap[slot].store(pedalID);
                        isPedalUsed[pedalID] = true;
                    }
                    else {
                        routingMap[slot].store(-1);
                    }
                }
                for (int slot = 0; slot < 20; ++slot) {
                    if (routingMap[slot].load() == -1) {
                        for (int p = 0; p < 20; ++p) {
                            if (!isPedalUsed[p]) { routingMap[slot].store(p); isPedalUsed[p] = true; break; }
                        }
                    }
                }
            }

            xmlState->deleteAllChildElementsWithTagName("ROUTING");
            if (xmlState->hasTagName(apvts.state.getType())) {
                apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
            }
        }
    }

private:
    // BUILDS THE DATABASE
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // Add every parameter here. (ID, Name, Min, Max, Default)
        // Distortion
        layout.add(std::make_unique<juce::AudioParameterFloat>("drive", "Drive", 1.0f, 100.0f, 50.0f));
        // Tremolo
        layout.add(std::make_unique<juce::AudioParameterFloat>("trem_depth", "Tremolo Depth", 0.0f, 1.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("trem_rate", "Tremolo Rate", 0.1f, 20.0f, 5.0f));
        // Delay
        layout.add(std::make_unique<juce::AudioParameterFloat>("delay_time", "Delay Time", 0.01f, 1.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("delay_feed", "Delay Feedback", 0.0f, 0.9f, 0.3f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("delay_mix", "Delay Mix", 0.0f, 1.0f, 0.0f));
        // Reverb
        layout.add(std::make_unique<juce::AudioParameterFloat>("rvb_room", "Room Size", 0.0f, 1.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("rvb_damp", "Damping", 0.0f, 1.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("rvb_mix", "Reverb Mix", 0.0f, 1.0f, 0.2f));
        // Chorus
        layout.add(std::make_unique<juce::AudioParameterFloat>("cho_rate", "Chorus Rate", 0.1f, 10.0f, 1.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("cho_depth", "Chorus Depth", 0.0f, 1.0f, 0.3f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("cho_mix", "Chorus Mix", 0.0f, 1.0f, 0.0f));
        // Flanger
        layout.add(std::make_unique<juce::AudioParameterFloat>("flg_rate", "Flanger Rate", 0.1f, 10.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("flg_depth", "Flanger Depth", 0.0f, 1.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("flg_feed", "Flanger Feedback", -0.9f, 0.9f, 0.5f));
        // Phaser
        layout.add(std::make_unique<juce::AudioParameterFloat>("phs_rate", "Phaser Rate", 0.1f, 10.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("phs_depth", "Phaser Depth", 0.0f, 1.0f, 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("phs_freq", "Phaser Center Freq", 100.0f, 2000.0f, 500.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("phs_feed", "Phaser Feedback", -0.9f, 0.9f, 0.3f));
        // Auto-Wah
        layout.add(std::make_unique<juce::AudioParameterFloat>("wah_rate", "Wah Rate", 0.1f, 10.0f, 2.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("wah_depth", "Wah Depth", 0.0f, 1.0f, 0.8f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("wah_q", "Wah Resonance", 1.0f, 10.0f, 5.0f));
        // Compressor
        layout.add(std::make_unique<juce::AudioParameterFloat>("cmp_thresh", "Threshold", -60.0f, 0.0f, -20.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("cmp_ratio", "Ratio", 1.0f, 20.0f, 4.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("cmp_att", "Attack", 1.0f, 100.0f, 10.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("cmp_rel", "Release", 10.0f, 1000.0f, 100.0f));
        // Boost
        layout.add(std::make_unique<juce::AudioParameterFloat>("bst_gain", "Boost (dB)", 0.0f, 24.0f, 0.0f));
        // EQ
        layout.add(std::make_unique<juce::AudioParameterFloat>("eq_low", "Low (dB)", -15.0f, 15.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("eq_mid", "Mid (dB)", -15.0f, 15.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("eq_high", "High (dB)", -15.0f, 15.0f, 0.0f));
        // Noise Gate
        layout.add(std::make_unique<juce::AudioParameterFloat>("ng_thresh", "Threshold", -60.0f, 0.0f, -20.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("ng_ratio", "Ratio", 1.0f, 20.0f, 4.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("ng_att", "Attack", 1.0f, 100.0f, 10.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("ng_rel", "Release", 10.0f, 1000.0f, 100.0f));
        // Pitch Shifter (Snaps strictly to 1-semitone steps)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "ps_semi", "Semitones",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 1.0f), 0.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>("ps_mix", "Pitch Mix", 0.0f, 1.0f, 0.0f));
        // Octaver (Snaps strictly to 12-semitone octave steps)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "oct_semi", "Octave Semitones",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 12.0f), 0.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>("oct_mix", "Octave Mix", 0.0f, 1.0f, 0.0f));
        // Acoustic
        layout.add(std::make_unique<juce::AudioParameterFloat>("ac_body", "Body Resonance", 0.0f, 15.0f, 5.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("ac_air", "String Air", 0.0f, 15.0f, 5.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("ac_reso", "Wood Mix", 0.0f, 1.0f, 0.3f));
        // Synth
        layout.add(std::make_unique<juce::AudioParameterChoice>("syn_type", "Waveform", juce::StringArray{ "Sine", "Square", "Saw" }, 1));
        layout.add(std::make_unique<juce::AudioParameterFloat>("syn_mix", "Synth Mix", 0.0f, 1.0f, 0.5f));

        // Looper 
        layout.add(std::make_unique<juce::AudioParameterChoice>("loop_state", "State", juce::StringArray{ "Stop", "Record", "Play", "Dub" }, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>("loop_level", "Loop Vol", 0.0f, 1.0f, 0.8f));

        // Bitcrusher
        layout.add(std::make_unique<juce::AudioParameterFloat>("bc_bits", "Bit Depth", 2.0f, 24.0f, 8.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("bc_down", "Downsample", 1.0f, 50.0f, 10.0f));

        // Ring Modulator
        layout.add(std::make_unique<juce::AudioParameterFloat>("rm_freq", "Ring Freq", 20.0f, 2000.0f, 500.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("rm_mix", "Ring Mix", 0.0f, 1.0f, 0.5f));

        // Master Volume
        layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 3.0f, 1.0f));

        // Distortion Type (0 = Tube, 1 = Overdrive, 2 = Fuzz)
        layout.add(std::make_unique<juce::AudioParameterChoice>("dist_type", "Type", juce::StringArray{ "Tube", "Overdrive", "Fuzz" }, 0));

        // Bypass Switches (True = Off/Bypassed)
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_0", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_1", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_2", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_3", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_4", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_5", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_6", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_7", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_8", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_9", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_10", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_11", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_12", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_13", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_14", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_15", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_16", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_17", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_18", "Bypass", true));
        layout.add(std::make_unique<juce::AudioParameterBool>("byp_19", "Bypass", true));

        return layout;
    }
};