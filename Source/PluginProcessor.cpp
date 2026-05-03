#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpMath.h"
#include "BinaryData.h"
#include "Effects.h"

// --- THE PROCESSOR (Audio Engine) ---
class MyAmpSimAudioProcessor : public juce::AudioProcessor
{
public:
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
        pedalboard.push_back(std::make_unique<DistortionPedal>(apvts.getRawParameterValue("drive")));
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
    }

    ~MyAmpSimAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
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

        // Process audio through the entire pedalboard dynamically
        for (auto& pedal : pedalboard)
        {
            pedal->process(buffer);
        }

        // Apply Master Volume at the very end
        float currentVol = apvts.getRawParameterValue("master_vol")->load();
        buffer.applyGain(currentVol);
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

        // Master Volume
        layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 3.0f, 1.0f));

        return layout;
    }
};

// --- THE GUI ---
class MyAmpSimEditor : public juce::AudioProcessorEditor
{
public:
    MyAmpSimEditor(MyAmpSimAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
    {
        setSize(1000, 380);

        // Row 1 Buttons (IDs 0-5)
        setupButton(btnComp, "Comp", 0); setupButton(btnBoost, "Boost", 1);
        setupButton(btnDistortion, "Dist", 2); setupButton(btnEq, "EQ", 3);
        setupButton(btnCabinet, "Cab", 4); setupButton(btnWah, "AutoWah", 5);

        // Row 2 Buttons (IDs 6-11)
        setupButton(btnPhaser, "Phaser", 6); setupButton(btnFlanger, "Flanger", 7);
        setupButton(btnTremolo, "Tremolo", 8); setupButton(btnChorus, "Chorus", 9);
        setupButton(btnDelay, "Delay", 10); setupButton(btnReverb, "Reverb", 11);

        // Setup Sliders (addChildComponent so they start invisible)
        setupKnob(cmpThreshKnob); setupKnob(cmpRatioKnob); setupKnob(cmpAttKnob); setupKnob(cmpRelKnob);
        setupKnob(bstGainKnob); setupKnob(driveKnob);
        setupKnob(eqLowKnob); setupKnob(eqMidKnob); setupKnob(eqHighKnob);
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
        cmpThreshAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_thresh", cmpThreshKnob);
        cmpRatioAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_ratio", cmpRatioKnob);
        cmpAttAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_att", cmpAttKnob);
        cmpRelAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "cmp_rel", cmpRelKnob);

        bstGainAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "bst_gain", bstGainKnob);
        driveAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "drive", driveKnob);

        eqLowAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "eq_low", eqLowKnob);
        eqMidAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "eq_mid", eqMidKnob);
        eqHighAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "eq_high", eqHighKnob);

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

        // Force UI to update based on the default selected pedal (0)
        updateVisibilities();
    }

    // Sets up a Rack Button
    void setupButton(juce::TextButton& btn, const juce::String& text, int id)
    {
        btn.setButtonText(text);

        // This Lambda fires when you click the button
        btn.onClick = [this, id] {
            currentPedal = id;       // Update selected pedal
            updateVisibilities();    // Show/Hide relevant knobs
            repaint();               // Redraw text labels
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
        cmpThreshKnob.setVisible(false); cmpRatioKnob.setVisible(false); cmpAttKnob.setVisible(false); cmpRelKnob.setVisible(false);
        bstGainKnob.setVisible(false); driveKnob.setVisible(false);
        eqLowKnob.setVisible(false); eqMidKnob.setVisible(false); eqHighKnob.setVisible(false);
        wahRateKnob.setVisible(false); wahDepthKnob.setVisible(false); wahQKnob.setVisible(false);
        phsRateKnob.setVisible(false); phsDepthKnob.setVisible(false); phsFreqKnob.setVisible(false); phsFeedKnob.setVisible(false);
        flgRateKnob.setVisible(false); flgDepthKnob.setVisible(false); flgFeedKnob.setVisible(false);
        tremDepthKnob.setVisible(false); tremRateKnob.setVisible(false);
        choRateKnob.setVisible(false); choDepthKnob.setVisible(false); choMixKnob.setVisible(false);
        dTimeKnob.setVisible(false); dFeedKnob.setVisible(false); dMixKnob.setVisible(false);
        rvbRoomKnob.setVisible(false); rvbDampKnob.setVisible(false); rvbMixKnob.setVisible(false);

        // Show Based on Selection
        if (currentPedal == 0) { cmpThreshKnob.setVisible(true); cmpRatioKnob.setVisible(true); cmpAttKnob.setVisible(true); cmpRelKnob.setVisible(true); }
        if (currentPedal == 1) { bstGainKnob.setVisible(true); }
        if (currentPedal == 2) { driveKnob.setVisible(true); }
        if (currentPedal == 3) { eqLowKnob.setVisible(true); eqMidKnob.setVisible(true); eqHighKnob.setVisible(true); }
        if (currentPedal == 5) { wahRateKnob.setVisible(true); wahDepthKnob.setVisible(true); wahQKnob.setVisible(true); }
        if (currentPedal == 6) { phsRateKnob.setVisible(true); phsDepthKnob.setVisible(true); phsFreqKnob.setVisible(true); phsFeedKnob.setVisible(true); }
        if (currentPedal == 7) { flgRateKnob.setVisible(true); flgDepthKnob.setVisible(true); flgFeedKnob.setVisible(true); }
        if (currentPedal == 8) { tremDepthKnob.setVisible(true); tremRateKnob.setVisible(true); }
        if (currentPedal == 9) { choRateKnob.setVisible(true); choDepthKnob.setVisible(true); choMixKnob.setVisible(true); }
        if (currentPedal == 10) { dTimeKnob.setVisible(true); dFeedKnob.setVisible(true); dMixKnob.setVisible(true); }
        if (currentPedal == 11) { rvbRoomKnob.setVisible(true); rvbDampKnob.setVisible(true); rvbMixKnob.setVisible(true); }

        volKnob.setVisible(true);

        // Trigger a UI recalculation
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
        g.setColour(juce::Colours::white);
        g.setFont(20.0f);

        // Draw the top Rack Bar
        auto rackArea = getLocalBounds().removeFromTop(80);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(rackArea);

        // Reset text color
        g.setColour(juce::Colours::darkred);
		g.drawLine(0, 40, 1000, 40, 2.0f); 

		g.setColour(juce::Colours::white);
        int yLabel = 140;

        // Draw Text Labels dynamically 
        if (currentPedal == 0) { g.drawText("Thresh", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Ratio", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Attack", 310, yLabel, 100, 20, juce::Justification::centred); g.drawText("Release", 440, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 1) { g.drawText("Boost dB", 50, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 2) { g.drawText("Drive", 50, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 3) { g.drawText("Low EQ", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Mid EQ", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("High EQ", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 4) { g.drawText("Cabinet IR Loaded.", 50, yLabel + 50, 400, 20, juce::Justification::centredLeft); }
        else if (currentPedal == 5) { g.drawText("Wah Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Wah Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Resonance", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 6) { g.drawText("Phs Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Phs Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Center Freq", 310, yLabel, 100, 20, juce::Justification::centred); g.drawText("Feedback", 440, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 7) { g.drawText("Flg Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Flg Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Feedback", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 8) { g.drawText("Trem Depth", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Trem Rate", 180, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 9) { g.drawText("Cho Rate", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Cho Depth", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Cho Mix", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 10) { g.drawText("Dly Time", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Dly Feed", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Dly Mix", 310, yLabel, 100, 20, juce::Justification::centred); }
        else if (currentPedal == 11) { g.drawText("Room Size", 50, yLabel, 100, 20, juce::Justification::centred); g.drawText("Damping", 180, yLabel, 100, 20, juce::Justification::centred); g.drawText("Rvb Mix", 310, yLabel, 100, 20, juce::Justification::centred); }

        g.drawText("Master Vol", 850, yLabel, 100, 20, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto rackTop = area.removeFromTop(40);
        auto rackBottom = area.removeFromTop(40);

		int numButtons = 6;
		int btnWidth = 1000 / numButtons;

        // Top Row
        btnComp.setBounds(rackTop.removeFromLeft(btnWidth).reduced(2));
        btnBoost.setBounds(rackTop.removeFromLeft(btnWidth).reduced(2));
        btnDistortion.setBounds(rackTop.removeFromLeft(btnWidth).reduced(2));
        btnEq.setBounds(rackTop.removeFromLeft(btnWidth).reduced(2));
        btnCabinet.setBounds(rackTop.removeFromLeft(btnWidth).reduced(2));
        btnWah.setBounds(rackTop.removeFromLeft(btnWidth).reduced(2));

        // Bottom Row
        btnPhaser.setBounds(rackBottom.removeFromLeft(btnWidth).reduced(2));
        btnFlanger.setBounds(rackBottom.removeFromLeft(btnWidth).reduced(2));
        btnTremolo.setBounds(rackBottom.removeFromLeft(btnWidth).reduced(2));
        btnChorus.setBounds(rackBottom.removeFromLeft(btnWidth).reduced(2));
        btnDelay.setBounds(rackBottom.removeFromLeft(btnWidth).reduced(2));
        btnReverb.setBounds(rackBottom.removeFromLeft(btnWidth).reduced(2));

        int yKnob = 170; // Shifted knobs down

        // Dynamic Knobs
        if (currentPedal == 0) { cmpThreshKnob.setBounds(50, yKnob, 100, 100); cmpRatioKnob.setBounds(180, yKnob, 100, 100); cmpAttKnob.setBounds(310, yKnob, 100, 100); cmpRelKnob.setBounds(440, yKnob, 100, 100); }
        if (currentPedal == 1) { bstGainKnob.setBounds(50, yKnob, 100, 100); }
        if (currentPedal == 2) { driveKnob.setBounds(50, yKnob, 100, 100); }
        if (currentPedal == 3) { eqLowKnob.setBounds(50, yKnob, 100, 100); eqMidKnob.setBounds(180, yKnob, 100, 100); eqHighKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 5) { wahRateKnob.setBounds(50, yKnob, 100, 100); wahDepthKnob.setBounds(180, yKnob, 100, 100); wahQKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 6) { phsRateKnob.setBounds(50, yKnob, 100, 100); phsDepthKnob.setBounds(180, yKnob, 100, 100); phsFreqKnob.setBounds(310, yKnob, 100, 100); phsFeedKnob.setBounds(440, yKnob, 100, 100); }
        if (currentPedal == 7) { flgRateKnob.setBounds(50, yKnob, 100, 100); flgDepthKnob.setBounds(180, yKnob, 100, 100); flgFeedKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 8) { tremDepthKnob.setBounds(50, yKnob, 100, 100); tremRateKnob.setBounds(180, yKnob, 100, 100); }
        if (currentPedal == 9) { choRateKnob.setBounds(50, yKnob, 100, 100); choDepthKnob.setBounds(180, yKnob, 100, 100); choMixKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 10) { dTimeKnob.setBounds(50, yKnob, 100, 100); dFeedKnob.setBounds(180, yKnob, 100, 100); dMixKnob.setBounds(310, yKnob, 100, 100); }
        if (currentPedal == 11) { rvbRoomKnob.setBounds(50, yKnob, 100, 100); rvbDampKnob.setBounds(180, yKnob, 100, 100); rvbMixKnob.setBounds(310, yKnob, 100, 100); }

        volKnob.setBounds(850, yKnob, 100, 100);
    }

private:
    MyAmpSimAudioProcessor& audioProcessor;

	int currentPedal = 0; 

    // UI Elements
    juce::TextButton btnComp, btnBoost, btnDistortion, btnEq, btnCabinet, btnWah;
    juce::TextButton btnPhaser, btnFlanger, btnTremolo, btnChorus, btnDelay, btnReverb;

    juce::Slider cmpThreshKnob, cmpRatioKnob, cmpAttKnob, cmpRelKnob;
    juce::Slider bstGainKnob, driveKnob;
    juce::Slider eqLowKnob, eqMidKnob, eqHighKnob;
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
    std::unique_ptr<Attachment> cmpThreshAttachment, cmpRatioAttachment, cmpAttAttachment, cmpRelAttachment;
    std::unique_ptr<Attachment> bstGainAttachment, driveAttachment;
    std::unique_ptr<Attachment> eqLowAttachment, eqMidAttachment, eqHighAttachment;
    std::unique_ptr<Attachment> wahRateAttachment, wahDepthAttachment, wahQAttachment;
    std::unique_ptr<Attachment> phsRateAttachment, phsDepthAttachment, phsFreqAttachment, phsFeedAttachment;
    std::unique_ptr<Attachment> flgRateAttachment, flgDepthAttachment, flgFeedAttachment;
    std::unique_ptr<Attachment> tremDepthAttachment, tremRateAttachment;
    std::unique_ptr<Attachment> choRateAttachment, choDepthAttachment, choMixAttachment;
    std::unique_ptr<Attachment> dTimeAttachment, dFeedAttachment, dMixAttachment;
    std::unique_ptr<Attachment> rvbRoomAttachment, rvbDampAttachment, rvbMixAttachment;
    std::unique_ptr<Attachment> volAttachment;
};

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }