#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpMath.h"
#include "BinaryData.h"
#include "Effects.h"

// --- THE PROCESSOR (Audio Engine) ---
class MyAmpSimAudioProcessor : public juce::AudioProcessor
{
public:
    // 1. THE CENTRAL DATABASE
    juce::AudioProcessorValueTreeState apvts;

    // 2. THE PEDALBOARD
    std::vector<std::unique_ptr<AudioEffect>> pedalboard;

    MyAmpSimAudioProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        // Initialize the APVTS with our parameter layout
        apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        // Assemble the signal chain, passing pointers to the raw data in our database
        pedalboard.push_back(std::make_unique<DistortionPedal>(apvts.getRawParameterValue("drive")));
        pedalboard.push_back(std::make_unique<CabinetPedal>());
        pedalboard.push_back(std::make_unique<TremoloPedal>(apvts.getRawParameterValue("trem_depth"), apvts.getRawParameterValue("trem_rate")));
        pedalboard.push_back(std::make_unique<ChorusPedal>(apvts.getRawParameterValue("cho_rate"), apvts.getRawParameterValue("cho_depth"), apvts.getRawParameterValue("cho_mix")));
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
    // 3. THIS FUNCTION BUILDS THE DATABASE
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

        layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 2.0f, 1.0f));



        return layout;
    }
};

// --- THE GUI ---
class MyAmpSimEditor : public juce::AudioProcessorEditor
{
public:
    MyAmpSimEditor(MyAmpSimAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
    {
        setSize(900, 300);

        // 1. Setup Top Rack Buttons
        setupButton(btnDistortion, "Distortion", 0);
        setupButton(btnCabinet, "Cabinet", 1);
        setupButton(btnTremolo, "Tremolo", 2);
        setupButton(btnDelay, "Chorus", 3);
		setupButton(btnReverb, "Delay", 4);
		setupButton(btnChorus, "Reverb", 5);

        // 2. Setup Sliders (addChildComponent so they start invisible)
        setupKnob(driveKnob);
        setupKnob(tremDepthKnob);
        setupKnob(tremRateKnob);
        setupKnob(choRateKnob);
        setupKnob(choDepthKnob);
		setupKnob(choMixKnob);
        setupKnob(dTimeKnob);
        setupKnob(dFeedKnob);
        setupKnob(dMixKnob);
        setupKnob(rvbRoomKnob);
        setupKnob(rvbDampKnob);
        setupKnob(rvbMixKnob);
        setupKnob(volKnob);

        // 3. Bind Sliders to APVTS Database explicitly
        using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "drive", driveKnob);

        tremDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "trem_depth", tremDepthKnob);
        tremRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "trem_rate", tremRateKnob);

        choRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "cho_rate", choRateKnob);
        choDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "cho_depth", choDepthKnob);
        choMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "cho_mix", choMixKnob);

        dTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "delay_time", dTimeKnob);
        dFeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "delay_feed", dFeedKnob);
        dMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "delay_mix", dMixKnob);

        rvbRoomAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "rvb_room", rvbRoomKnob);
        rvbDampAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "rvb_damp", rvbDampKnob);
        rvbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "rvb_mix", rvbMixKnob);

        volAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "master_vol", volKnob);

        // 4. Force UI to update based on the default selected pedal (0)
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
        driveKnob.setVisible(false);
        tremDepthKnob.setVisible(false);
        tremRateKnob.setVisible(false);
		choRateKnob.setVisible(false);
		choDepthKnob.setVisible(false);
		choMixKnob.setVisible(false);
        dTimeKnob.setVisible(false);
        dFeedKnob.setVisible(false);
        dMixKnob.setVisible(false);
		rvbRoomKnob.setVisible(false);
		rvbDampKnob.setVisible(false);
        rvbMixKnob.setVisible(false);


        // Show only the selected pedal's knobs
        if (currentPedal == 0) { driveKnob.setVisible(true); }
        // Cabinet (currentPedal == 1) has no knobs
        if (currentPedal == 2) { tremDepthKnob.setVisible(true); tremRateKnob.setVisible(true); }
        if (currentPedal == 3) { choRateKnob.setVisible(true); choDepthKnob.setVisible(true); choMixKnob.setVisible(true); }
		if (currentPedal == 4) { dTimeKnob.setVisible(true); dFeedKnob.setVisible(true); dMixKnob.setVisible(true); }
		if (currentPedal == 5) { rvbRoomKnob.setVisible(true); rvbDampKnob.setVisible(true); rvbMixKnob.setVisible(true); }

        // Master Volume  always visible
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
        auto rackArea = getLocalBounds().removeFromTop(40);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(rackArea);

        // Reset text color
        g.setColour(juce::Colours::white);

        // Draw Text Labels dynamically based on what pedal is selected
        if (currentPedal == 0)
        {
            g.drawText("Drive", 50, 80, 100, 20, juce::Justification::centred);
        }
        else if (currentPedal == 1)
        {
            g.drawText("Cabinet IR Loaded. No parameters to edit.", 50, 150, 400, 20, juce::Justification::centredLeft);
        }
        else if (currentPedal == 2)
        {
            g.drawText("Depth", 50, 80, 100, 20, juce::Justification::centred);
            g.drawText("Rate", 180, 80, 100, 20, juce::Justification::centred);
        }
        else if (currentPedal == 3)
        {
            g.drawText("Rate", 50, 80, 100, 20, juce::Justification::centred);
            g.drawText("Depth", 180, 80, 100, 20, juce::Justification::centred);
            g.drawText("Mix", 310, 80, 100, 20, juce::Justification::centred);
		}
        else if (currentPedal == 4)
        {
            g.drawText("Time", 50, 80, 100, 20, juce::Justification::centred);
            g.drawText("Feedback", 180, 80, 100, 20, juce::Justification::centred);
            g.drawText("Mix", 310, 80, 100, 20, juce::Justification::centred);
        }
        else if (currentPedal == 5)
        {
            g.drawText("Room Size", 50, 80, 100, 20, juce::Justification::centred);
            g.drawText("Damping", 180, 80, 100, 20, juce::Justification::centred);
            g.drawText("Mix", 310, 80, 100, 20, juce::Justification::centred);
		}

        // Master Volume label is permanent
        g.drawText("Master Vol", 750, 80, 100, 20, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto rackArea = area.removeFromTop(40);

        // Position the Top Buttons
        int btnWidth = 120;
        btnDistortion.setBounds(rackArea.removeFromLeft(btnWidth).reduced(2));
        btnCabinet.setBounds(rackArea.removeFromLeft(btnWidth).reduced(2));
        btnTremolo.setBounds(rackArea.removeFromLeft(btnWidth).reduced(2));
        btnChorus.setBounds(rackArea.removeFromLeft(btnWidth).reduced(2));
        btnDelay.setBounds(rackArea.removeFromLeft(btnWidth).reduced(2));
		btnReverb.setBounds(rackArea.removeFromLeft(btnWidth).reduced(2));
		

        // Position the Dynamic Knobs in the Editor Area
        if (currentPedal == 0)
        {
            driveKnob.setBounds(50, 110, 100, 100);
        }
        if (currentPedal == 2)
        {
            tremDepthKnob.setBounds(50, 110, 100, 100);
            tremRateKnob.setBounds(180, 110, 100, 100);
        }
        if (currentPedal == 3)
        {
            choRateKnob.setBounds(50, 110, 100, 100);
            choDepthKnob.setBounds(180, 110, 100, 100);
            choMixKnob.setBounds(310, 110, 100, 100);
		}
        if (currentPedal == 4)
        {
            dTimeKnob.setBounds(50, 110, 100, 100);
            dFeedKnob.setBounds(180, 110, 100, 100);
            dMixKnob.setBounds(310, 110, 100, 100);
        }
        if (currentPedal == 5)
        {
			rvbRoomKnob.setBounds(50, 110, 100, 100);
			rvbDampKnob.setBounds(180, 110, 100, 100);
			rvbMixKnob.setBounds(310, 110, 100, 100);
        }
        // Position Master Vol on the far right
        volKnob.setBounds(750, 110, 100, 100);
    }

private:
    MyAmpSimAudioProcessor& audioProcessor;

	int currentPedal = 0; // State Tracker: 0=Dist, 1=Cab, 2=Trem, 3=Delay, 4=Reverb, 5=Chorus

    // UI Elements
    juce::TextButton btnDistortion, btnCabinet, btnTremolo, btnChorus, btnDelay, btnReverb;

    juce::Slider driveKnob, volKnob;
    juce::Slider tremDepthKnob, tremRateKnob;
    juce::Slider choRateKnob, choDepthKnob, choMixKnob;
    juce::Slider dTimeKnob, dFeedKnob, dMixKnob;
    juce::Slider rvbRoomKnob, rvbDampKnob, rvbMixKnob;

    // Explicit unique_ptrs instead of using an alias 
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, volAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tremDepthAttachment, tremRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> choRateAttachment, choDepthAttachment, choMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dTimeAttachment, dFeedAttachment, dMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rvbRoomAttachment, rvbDampAttachment, rvbMixAttachment;
};

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }