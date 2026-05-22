#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpMath.h"
#include "BinaryData.h"
#include "Effects.h"

// --- THE PROCESSOR (Audio Engine) ---
class MyAmpSimAudioProcessor : public juce::AudioProcessor
{
public:
    GuitarTuner tuner;
    std::atomic<float> currentPitchHz{ 0.0f }; // So the GUI can read it

    // THE CENTRAL DATABASE
    juce::AudioProcessorValueTreeState apvts;

    // THE PEDALBOARD
    std::vector<std::unique_ptr<AudioEffect>> pedalboard;

    MyAmpSimAudioProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        // Initialize the APVTS with our parameter layout
        apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        // Assemble the signal chain, passing pointers to the raw data in our database
        // 1. Dynamics
        pedalboard.push_back(std::make_unique<CompressorPedal>(apvts.getRawParameterValue("cmp_thresh"), apvts.getRawParameterValue("cmp_ratio"), apvts.getRawParameterValue("cmp_att"), apvts.getRawParameterValue("cmp_rel")));
        pedalboard.push_back(std::make_unique<BoosterPedal>(apvts.getRawParameterValue("bst_gain")));

        // 2. Drive & Tone
        pedalboard.push_back(std::make_unique<DistortionPedal>(apvts.getRawParameterValue("drive"), apvts.getRawParameterValue("dist_type")));
        pedalboard.push_back(std::make_unique<EqPedal>(apvts.getRawParameterValue("eq_low"), apvts.getRawParameterValue("eq_mid"), apvts.getRawParameterValue("eq_high")));
        pedalboard.push_back(std::make_unique<CabinetPedal>());

        // 3. Filters & Modulation
        pedalboard.push_back(std::make_unique<AutoWahPedal>(apvts.getRawParameterValue("wah_rate"), apvts.getRawParameterValue("wah_depth"), apvts.getRawParameterValue("wah_q")));
        pedalboard.push_back(std::make_unique<PhaserPedal>(apvts.getRawParameterValue("phs_rate"), apvts.getRawParameterValue("phs_depth"), apvts.getRawParameterValue("phs_freq"), apvts.getRawParameterValue("phs_feed")));
        pedalboard.push_back(std::make_unique<FlangerPedal>(apvts.getRawParameterValue("flg_rate"), apvts.getRawParameterValue("flg_depth"), apvts.getRawParameterValue("flg_feed")));
        pedalboard.push_back(std::make_unique<TremoloPedal>(apvts.getRawParameterValue("trem_depth"), apvts.getRawParameterValue("trem_rate")));
        pedalboard.push_back(std::make_unique<ChorusPedal>(apvts.getRawParameterValue("cho_rate"), apvts.getRawParameterValue("cho_depth"), apvts.getRawParameterValue("cho_mix")));

        // 4. Time
        pedalboard.push_back(std::make_unique<DelayPedal>(apvts.getRawParameterValue("delay_time"), apvts.getRawParameterValue("delay_feed"), apvts.getRawParameterValue("delay_mix")));
        pedalboard.push_back(std::make_unique<ReverbPedal>(apvts.getRawParameterValue("rvb_room"), apvts.getRawParameterValue("rvb_damp"), apvts.getRawParameterValue("rvb_mix")));
        
        for (int i = 0; i < pedalboard.size(); ++i) {
            juce::String bypassID = "byp_" + juce::String(i);
            pedalboard[i]->isBypassed = apvts.getRawParameterValue(bypassID);
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


        // Process audio through the entire pedalboard dynamically
        for (auto& pedal : pedalboard)
        {
            pedal->process(buffer);
        }

        // Apply Master Volume at the very end
        buffer.applyGain(apvts.getRawParameterValue("master_vol")->load());
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
    void getStateInformation(juce::MemoryBlock& destData) override {}
    void setStateInformation(const void* data, int sizeInBytes) override {}

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
		// Pitch Shifter
		layout.add(std::make_unique<juce::AudioParameterFloat>("ps_semi", "Semitones", -12.0f, 12.0f, 0.0f));
		layout.add(std::make_unique<juce::AudioParameterFloat>("ps_mix", "Pitch Mix", 0.0f, 1.0f, 0.0f));
		// Octaver
		layout.add(std::make_unique<juce::AudioParameterFloat>("oct_semi", "Octave Semitones", -24.0f, 24.0f, 0.0f));
		layout.add(std::make_unique<juce::AudioParameterFloat>("oct_mix", "Octave Mix", 0.0f, 1.0f, 0.0f));

        // Master Volume
        layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 3.0f, 1.0f));

		// Distortion Type (0 = Tube, 1 = Overdrive, 2 = Fuzz)
        layout.add(std::make_unique<juce::AudioParameterChoice>("dist_type", "Type", juce::StringArray{ "Tube", "Overdrive", "Fuzz" }, 0));
        // Add 12 Bypass Switches (True = Off/Bypassed)
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

        return layout;
    }
};

// --- THE GUI ---
class MyAmpSimEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    MyAmpSimEditor(MyAmpSimAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
    {
        setSize(1000, 380);

        // Row 1 (IDs 0-7)
        setupButton(btnGate, "Gate", 0); setupButton(btnComp, "Comp", 1);
        setupButton(btnBoost, "Boost", 2); setupButton(btnDistortion, "Dist", 3);
        setupButton(btnEq, "EQ", 4); setupButton(btnPitch, "Pitch", 5);
        setupButton(btnOctave, "Octaver", 6); setupButton(btnCabinet, "Cab", 7);

        // Row 2 (IDs 8-14)
        setupButton(btnWah, "AutoWah", 8); setupButton(btnPhaser, "Phaser", 9);
        setupButton(btnFlanger, "Flanger", 10); setupButton(btnTremolo, "Tremolo", 11);
        setupButton(btnChorus, "Chorus", 12); setupButton(btnDelay, "Delay", 13);
        setupButton(btnReverb, "Reverb", 14);

        // 2. SETUP UI CONTROLS
        bypassToggle.setButtonText("PEDAL OFF");
        addAndMakeVisible(bypassToggle);

        tunerToggle.setButtonText("SHOW TUNER");
        tunerToggle.onClick = [this] { repaint(); };
        addAndMakeVisible(tunerToggle);

        killAllButton.setButtonText("RESET ALL SETTINGS");
        killAllButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        killAllButton.onClick = [this] {
            // Loop through every parameter registered to the Audio Processor
            for (auto* param : audioProcessor.getParameters())
            {
                // Cast it to a ranged parameter
                if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*>(param))
                {
                    // Force it back to its default value and tell the DAW about it
                    rangedParam->setValueNotifyingHost(rangedParam->getDefaultValue());
                }
            }
            };
        addAndMakeVisible(killAllButton);

        distTypeBox.addItemList({ "Tube", "Overdrive", "Fuzz" }, 1);
        addChildComponent(distTypeBox);
        distTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "dist_type", distTypeBox);


        // Setup Sliders (addChildComponent so they start invisible)
        setupKnob(ngThreshKnob); setupKnob(ngRatioKnob); setupKnob(ngAttKnob); setupKnob(ngRelKnob);
        setupKnob(cmpThreshKnob); setupKnob(cmpRatioKnob); setupKnob(cmpAttKnob); setupKnob(cmpRelKnob);
        setupKnob(bstGainKnob); setupKnob(driveKnob);
        setupKnob(eqLowKnob); setupKnob(eqMidKnob); setupKnob(eqHighKnob);
        setupKnob(psSemiKnob); setupKnob(psMixKnob);
        setupKnob(octSemiKnob); setupKnob(octMixKnob);
        setupKnob(wahRateKnob); setupKnob(wahDepthKnob); setupKnob(wahQKnob);
        setupKnob(phsRateKnob); setupKnob(phsDepthKnob); setupKnob(phsFreqKnob); setupKnob(phsFeedKnob);
        setupKnob(flgRateKnob); setupKnob(flgDepthKnob); setupKnob(flgFeedKnob);
        setupKnob(tremDepthKnob); setupKnob(tremRateKnob);
        setupKnob(choRateKnob); setupKnob(choDepthKnob); setupKnob(choMixKnob);
        setupKnob(dTimeKnob); setupKnob(dFeedKnob); setupKnob(dMixKnob);
        setupKnob(rvbRoomKnob); setupKnob(rvbDampKnob); setupKnob(rvbMixKnob);
        setupKnob(volKnob);

        // Bind Sliders to APVTS
        using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        ngThreshAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "ng_thresh", ngThreshKnob);
        ngRatioAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "ng_ratio", ngRatioKnob);
        ngAttAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "ng_att", ngAttKnob);
        ngRelAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "ng_rel", ngRelKnob);

        cmpThreshAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_thresh", cmpThreshKnob);
        cmpRatioAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_ratio", cmpRatioKnob);
        cmpAttAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_att", cmpAttKnob);
        cmpRelAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_rel", cmpRelKnob);

        bstGainAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "bst_gain", bstGainKnob);
        driveAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "drive", driveKnob);

        eqLowAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "eq_low", eqLowKnob);
        eqMidAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "eq_mid", eqMidKnob);
        eqHighAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "eq_high", eqHighKnob);

        psSemiAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "ps_semi", psSemiKnob);
        psMixAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "ps_mix", psMixKnob);
        octSemiAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "oct_semi", octSemiKnob);
        octMixAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "oct_mix", octMixKnob);

        wahRateAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "wah_rate", wahRateKnob);
        wahDepthAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "wah_depth", wahDepthKnob);
        wahQAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "wah_q", wahQKnob);

        phsRateAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "phs_rate", phsRateKnob);
        phsDepthAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "phs_depth", phsDepthKnob);
        phsFreqAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "phs_freq", phsFreqKnob);
        phsFeedAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "phs_feed", phsFeedKnob);

        flgRateAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "flg_rate", flgRateKnob);
        flgDepthAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "flg_depth", flgDepthKnob);
        flgFeedAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "flg_feed", flgFeedKnob);

        tremDepthAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "trem_depth", tremDepthKnob);
        tremRateAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "trem_rate", tremRateKnob);

        choRateAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cho_rate", choRateKnob);
        choDepthAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cho_depth", choDepthKnob);
        choMixAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cho_mix", choMixKnob);

        dTimeAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "delay_time", dTimeKnob);
        dFeedAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "delay_feed", dFeedKnob);
        dMixAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "delay_mix", dMixKnob);

        rvbRoomAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "rvb_room", rvbRoomKnob);
        rvbDampAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "rvb_damp", rvbDampKnob);
        rvbMixAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "rvb_mix", rvbMixKnob);

        volAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "master_vol", volKnob);

        updateVisibilities();
        startTimerHz(30);
    }

    void timerCallback() override
    {
        bool needsRepaint = false;

        // Safely check bypass states for all 15 pedals
        for (int i = 0; i < 15; ++i) {
            juce::String bypassID = "byp_" + juce::String(i);

            // 1. Get the pointer
            auto* paramPtr = audioProcessor.apvts.getRawParameterValue(bypassID);

            // 2. Only load() if the pointer actually exists
            if (paramPtr != nullptr)
            {
                bool isOff = paramPtr->load() > 0.5f;
                if (pedalStates[i] != isOff) {
                    pedalStates[i] = isOff;
                    needsRepaint = true;
                }
            }
        }

        float currentHz = audioProcessor.currentPitchHz.load();
        if (std::abs(currentHz - lastHz) > 1.0f) {
            lastHz = currentHz;
            needsRepaint = true;
        }

        if (needsRepaint) repaint();
    }

    // Sets up a Rack Button
    void setupButton(juce::TextButton& btn, const juce::String& text, int id)
    {
        btn.setButtonText(text);

        // This Lambda fires when you click the button
        btn.onClick = [this, id] {
            currentPedal = id;       // Update selected pedal
            updateVisibilities();    // Show/Hide relevant knobs
            repaint();
            };
        addAndMakeVisible(btn);
    }

    // Sets up a Knob
    void setupKnob(juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::Rotary);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        addChildComponent(slider); // adds it to UI but keeps it hidden
    }

    // Decides what is visible right now
    void updateVisibilities()
    {
        // Hide everything
        ngThreshKnob.setVisible(false); ngRatioKnob.setVisible(false); ngAttKnob.setVisible(false); ngRelKnob.setVisible(false);
        cmpThreshKnob.setVisible(false); cmpRatioKnob.setVisible(false); cmpAttKnob.setVisible(false); cmpRelKnob.setVisible(false);
        bstGainKnob.setVisible(false); driveKnob.setVisible(false); distTypeBox.setVisible(false);
        eqLowKnob.setVisible(false); eqMidKnob.setVisible(false); eqHighKnob.setVisible(false);
        psSemiKnob.setVisible(false); psMixKnob.setVisible(false);
        octSemiKnob.setVisible(false); octMixKnob.setVisible(false);
        wahRateKnob.setVisible(false); wahDepthKnob.setVisible(false); wahQKnob.setVisible(false);
        phsRateKnob.setVisible(false); phsDepthKnob.setVisible(false); phsFreqKnob.setVisible(false); phsFeedKnob.setVisible(false);
        flgRateKnob.setVisible(false); flgDepthKnob.setVisible(false); flgFeedKnob.setVisible(false);
        tremDepthKnob.setVisible(false); tremRateKnob.setVisible(false);
        choRateKnob.setVisible(false); choDepthKnob.setVisible(false); choMixKnob.setVisible(false);
        dTimeKnob.setVisible(false); dFeedKnob.setVisible(false); dMixKnob.setVisible(false);
        rvbRoomKnob.setVisible(false); rvbDampKnob.setVisible(false); rvbMixKnob.setVisible(false);

        // Show Based on Selection
        if (currentPedal == 0) { ngThreshKnob.setVisible(true); ngRatioKnob.setVisible(true); ngAttKnob.setVisible(true); ngRelKnob.setVisible(true); }
        if (currentPedal == 1) { cmpThreshKnob.setVisible(true); cmpRatioKnob.setVisible(true); cmpAttKnob.setVisible(true); cmpRelKnob.setVisible(true); }
        if (currentPedal == 2) { bstGainKnob.setVisible(true); }
        if (currentPedal == 3) { driveKnob.setVisible(true); distTypeBox.setVisible(true); }
        if (currentPedal == 4) { eqLowKnob.setVisible(true); eqMidKnob.setVisible(true); eqHighKnob.setVisible(true); }
        if (currentPedal == 5) { psSemiKnob.setVisible(true); psMixKnob.setVisible(true); }
        if (currentPedal == 6) { octSemiKnob.setVisible(true); octMixKnob.setVisible(true); }
        if (currentPedal == 8) { wahRateKnob.setVisible(true); wahDepthKnob.setVisible(true); wahQKnob.setVisible(true); }
        if (currentPedal == 9) { phsRateKnob.setVisible(true); phsDepthKnob.setVisible(true); phsFreqKnob.setVisible(true); phsFeedKnob.setVisible(true); }
        if (currentPedal == 10) { flgRateKnob.setVisible(true); flgDepthKnob.setVisible(true); flgFeedKnob.setVisible(true); }
        if (currentPedal == 11) { tremDepthKnob.setVisible(true); tremRateKnob.setVisible(true); }
        if (currentPedal == 12) { choRateKnob.setVisible(true); choDepthKnob.setVisible(true); choMixKnob.setVisible(true); }
        if (currentPedal == 13) { dTimeKnob.setVisible(true); dFeedKnob.setVisible(true); dMixKnob.setVisible(true); }
        if (currentPedal == 14) { rvbRoomKnob.setVisible(true); rvbDampKnob.setVisible(true); rvbMixKnob.setVisible(true); }

        volKnob.setVisible(true);

        juce::String bypassID = "byp_" + juce::String(currentPedal);
        bypassAttachment.reset(); // Destroy old binding
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, bypassID, bypassToggle);

        // Trigger a UI recalculation
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(getLocalBounds().removeFromTop(80));
        g.setColour(juce::Colours::darkred);
        g.drawLine(0, 40, 1000, 40, 2.0f);

        // COLOR LOGIC: Blue = Selected, Green = ON, Grey = OFF
        auto getColor = [this](int id) {
            if (currentPedal == id) return juce::Colours::dodgerblue;
            return pedalStates[id] ? juce::Colours::darkgrey : juce::Colours::limegreen;
            };

        btnGate.setColour(juce::TextButton::buttonColourId, getColor(0));
        btnComp.setColour(juce::TextButton::buttonColourId, getColor(1));
        btnBoost.setColour(juce::TextButton::buttonColourId, getColor(2));
        btnDistortion.setColour(juce::TextButton::buttonColourId, getColor(3));
        btnEq.setColour(juce::TextButton::buttonColourId, getColor(4));
        btnPitch.setColour(juce::TextButton::buttonColourId, getColor(5));
        btnOctave.setColour(juce::TextButton::buttonColourId, getColor(6));
        btnCabinet.setColour(juce::TextButton::buttonColourId, getColor(7));

        btnWah.setColour(juce::TextButton::buttonColourId, getColor(8));
        btnPhaser.setColour(juce::TextButton::buttonColourId, getColor(9));
        btnFlanger.setColour(juce::TextButton::buttonColourId, getColor(10));
        btnTremolo.setColour(juce::TextButton::buttonColourId, getColor(11));
        btnChorus.setColour(juce::TextButton::buttonColourId, getColor(12));
        btnDelay.setColour(juce::TextButton::buttonColourId, getColor(13));
        btnReverb.setColour(juce::TextButton::buttonColourId, getColor(14));

        // Tuner Screen
        if (tunerToggle.getToggleState())
        {
            g.setColour(juce::Colours::black);
            g.fillRect(700, 150, 130, 80);
            g.setColour(juce::Colours::limegreen);

            juce::String noteName = "--";
			if (lastHz > 40.0f) { // Only display if we have a valid frequency
                int midiNote = std::round(69 + 12 * std::log2(lastHz / 440.0));
                juce::StringArray notes = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                noteName = notes[midiNote % 12];
                g.setFont(15.0f);
                g.drawText(juce::String(lastHz, 1) + " Hz", 700, 190, 130, 30, juce::Justification::centred);
            }
            g.setFont(30.0f);
            g.drawText(noteName, 700, 150, 130, 50, juce::Justification::centred);
        }

        g.setColour(juce::Colours::white);
        g.setFont(20.0f);
        int yLabel = 140;

        // Draw parameter labels
        if (currentPedal == 0) { g.drawText("Thresh", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Ratio", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Attack", 310, yLabel, 100, 20, juce::Justification::centred); g.drawText("Release", 440, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 1) { g.drawText("Thresh", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Ratio", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Attack", 310, yLabel, 100, 20, juce::Justification::centred); g.drawText("Release", 440, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 2) { g.drawText("Boost dB", 50, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 3) { g.drawText("Drive", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Type", 180, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 4) { g.drawText("Low EQ", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Mid EQ", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("High EQ", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 5) { g.drawText("Shift", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Mix", 180, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 6) { g.drawText("Octave", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Mix", 180, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 7) { g.drawText("Cabinet IR Loaded.", 50, yLabel + 50, 400, 20, juce::Justification::centredLeft); }
        else if (currentPedal == 8) { g.drawText("Wah Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Wah Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Resonance", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 9) { g.drawText("Phs Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Phs Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Center Freq", 310, yLabel, 100, 20, juce::Justification::centred); g.drawText("Feedback", 440, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 10) { g.drawText("Flg Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Flg Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Feedback", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 11) { g.drawText("Trem Depth", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Trem Rate", 180, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 12) { g.drawText("Cho Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Cho Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Cho Mix", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 13) { g.drawText("Dly Time", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Dly Feed", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Dly Mix", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 14) { g.drawText("Room Size", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Damping", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Rvb Mix", 310, yLabel, 100, 20, juce::Justification::centred); }

        g.drawText("Master Vol", 850, yLabel, 100, 20, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto rackTop = area.removeFromTop(40);
        auto rackBottom = area.removeFromTop(40);

        int topWidth = 1000 / 8;
        btnGate.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnComp.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnBoost.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnDistortion.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnEq.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnPitch.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnOctave.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));
        btnCabinet.setBounds(rackTop.removeFromLeft(topWidth).reduced(2));

        int botWidth = 1000 / 7;
        btnWah.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));
        btnPhaser.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));
        btnFlanger.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));
        btnTremolo.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));
        btnChorus.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));
        btnDelay.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));
        btnReverb.setBounds(rackBottom.removeFromLeft(botWidth).reduced(2));

        bypassToggle.setBounds(50, 100, 150, 30);
        tunerToggle.setBounds(700, 100, 120, 30);
        killAllButton.setBounds(850, 100, 150, 30);

        int yKnob = 170; // Shifted knobs down

        // Dynamic Knobs
        if (currentPedal == 0) { ngThreshKnob.setBounds(50, yKnob, 100, 100); ngRatioKnob.setBounds(180, yKnob, 100, 100); ngAttKnob.setBounds(310, yKnob, 100, 100); ngRelKnob.setBounds(440, yKnob, 100, 100); }
        if (currentPedal == 1) { cmpThreshKnob.setBounds(50, yKnob, 100, 100); cmpRatioKnob.setBounds(180, yKnob, 100, 100); cmpAttKnob.setBounds(310, yKnob, 100, 100); cmpRelKnob.setBounds(440, yKnob, 100, 100); }
        if (currentPedal == 2) { bstGainKnob.setBounds(50, yKnob, 100, 100); }
        if (currentPedal == 3) { driveKnob.setBounds(50, yKnob, 100, 100); distTypeBox.setBounds(180, yKnob + 30, 100, 30); }
        if (currentPedal == 4) { eqLowKnob.setBounds(50, yKnob, 100, 100); eqMidKnob.setBounds(180, yKnob, 100, 100); eqHighKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 5) { psSemiKnob.setBounds(50, yKnob, 100, 100); psMixKnob.setBounds(180, yKnob, 100, 100); }
        if (currentPedal == 6) { octSemiKnob.setBounds(50, yKnob, 100, 100); octMixKnob.setBounds(180, yKnob, 100, 100); }
        if (currentPedal == 8) { wahRateKnob.setBounds(50, yKnob, 100, 100); wahDepthKnob.setBounds(180, yKnob, 100, 100); wahQKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 9) { phsRateKnob.setBounds(50, yKnob, 100, 100); phsDepthKnob.setBounds(180, yKnob, 100, 100); phsFreqKnob.setBounds(310, yKnob, 100, 100); phsFeedKnob.setBounds(440, yKnob, 100, 100); }
        if (currentPedal == 10) { flgRateKnob.setBounds(50, yKnob, 100, 100); flgDepthKnob.setBounds(180, yKnob, 100, 100); flgFeedKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 11) { tremDepthKnob.setBounds(50, yKnob, 100, 100); tremRateKnob.setBounds(180, yKnob, 100, 100); }
        if (currentPedal == 12) { choRateKnob.setBounds(50, yKnob, 100, 100); choDepthKnob.setBounds(180, yKnob, 100, 100); choMixKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 13) { dTimeKnob.setBounds(50, yKnob, 100, 100); dFeedKnob.setBounds(180, yKnob, 100, 100); dMixKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 14) { rvbRoomKnob.setBounds(50, yKnob, 100, 100); rvbDampKnob.setBounds(180, yKnob, 100, 100); rvbMixKnob.setBounds(310, yKnob, 100, 100); }

        volKnob.setBounds(850, yKnob, 100, 100);
    }

private:
    MyAmpSimAudioProcessor& audioProcessor;
    int currentPedal = 0;

    float lastHz = 0.0f;
	bool pedalStates[15] = { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true }; // all bypassed by default

    juce::TextButton killAllButton;
    juce::ToggleButton bypassToggle, tunerToggle;
    juce::ComboBox distTypeBox;

    juce::TextButton btnGate, btnComp, btnBoost, btnDistortion, btnEq, btnPitch, btnOctave, btnCabinet;
    juce::TextButton btnWah, btnPhaser, btnFlanger, btnTremolo, btnChorus, btnDelay, btnReverb;

    juce::Slider ngThreshKnob, ngRatioKnob, ngAttKnob, ngRelKnob;
    juce::Slider cmpThreshKnob, cmpRatioKnob, cmpAttKnob, cmpRelKnob;
    juce::Slider bstGainKnob, driveKnob;
    juce::Slider eqLowKnob, eqMidKnob, eqHighKnob;
    juce::Slider psSemiKnob, psMixKnob;
    juce::Slider octSemiKnob, octMixKnob;
    juce::Slider wahRateKnob, wahDepthKnob, wahQKnob;
    juce::Slider phsRateKnob, phsDepthKnob, phsFreqKnob, phsFeedKnob;
    juce::Slider flgRateKnob, flgDepthKnob, flgFeedKnob;
    juce::Slider tremDepthKnob, tremRateKnob;
    juce::Slider choRateKnob, choDepthKnob, choMixKnob;
    juce::Slider dTimeKnob, dFeedKnob, dMixKnob;
    juce::Slider rvbRoomKnob, rvbDampKnob, rvbMixKnob;
    juce::Slider volKnob;

    // Explicit unique_ptrs instead of using an alias 
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> ngThreshAttachment, ngRatioAttachment, ngAttAttachment, ngRelAttachment;
    std::unique_ptr<Attachment> cmpThreshAttachment, cmpRatioAttachment, cmpAttAttachment, cmpRelAttachment;
    std::unique_ptr<Attachment> bstGainAttachment, driveAttachment;
    std::unique_ptr<Attachment> eqLowAttachment, eqMidAttachment, eqHighAttachment;
    std::unique_ptr<Attachment> psSemiAttachment, psMixAttachment;
    std::unique_ptr<Attachment> octSemiAttachment, octMixAttachment;
    std::unique_ptr<Attachment> wahRateAttachment, wahDepthAttachment, wahQAttachment;
    std::unique_ptr<Attachment> phsRateAttachment, phsDepthAttachment, phsFreqAttachment, phsFeedAttachment;
    std::unique_ptr<Attachment> flgRateAttachment, flgDepthAttachment, flgFeedAttachment;
    std::unique_ptr<Attachment> tremDepthAttachment, tremRateAttachment;
    std::unique_ptr<Attachment> choRateAttachment, choDepthAttachment, choMixAttachment;
    std::unique_ptr<Attachment> dTimeAttachment, dFeedAttachment, dMixAttachment;
    std::unique_ptr<Attachment> rvbRoomAttachment, rvbDampAttachment, rvbMixAttachment;
    std::unique_ptr<Attachment> volAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> distTypeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
};

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }