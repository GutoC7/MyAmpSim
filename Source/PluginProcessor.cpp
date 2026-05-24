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
    std::atomic<float> outputPeak{ 0.0f }; // Tracks the master output volume

    // THE CENTRAL DATABASE
    juce::AudioProcessorValueTreeState apvts;

    // THE PEDALBOARD
    std::vector<std::unique_ptr<AudioEffect>> pedalboard;
    
    std::atomic<int> routingMap[18];

    MyAmpSimAudioProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        // Initialize the APVTS with our parameter layout
        apvts(*this, nullptr, "Parameters", createParameterLayout())
    {
        // 0-7: Dynamics, Pitch, and Drive (Top Row)
        pedalboard.push_back(std::make_unique<NoiseGatePedal>(apvts.getRawParameterValue("ng_thresh"), apvts.getRawParameterValue("ng_ratio"), apvts.getRawParameterValue("ng_att"), apvts.getRawParameterValue("ng_rel")));
        pedalboard.push_back(std::make_unique<CompressorPedal>(apvts.getRawParameterValue("cmp_thresh"), apvts.getRawParameterValue("cmp_ratio"), apvts.getRawParameterValue("cmp_att"), apvts.getRawParameterValue("cmp_rel")));
        pedalboard.push_back(std::make_unique<BoosterPedal>(apvts.getRawParameterValue("bst_gain")));
        pedalboard.push_back(std::make_unique<DistortionPedal>(apvts.getRawParameterValue("drive"), apvts.getRawParameterValue("dist_type")));
        pedalboard.push_back(std::make_unique<EqPedal>(apvts.getRawParameterValue("eq_low"), apvts.getRawParameterValue("eq_mid"), apvts.getRawParameterValue("eq_high")));
        pedalboard.push_back(std::make_unique<PitchShifterPedal>(apvts.getRawParameterValue("ps_semi"), apvts.getRawParameterValue("ps_mix")));
        pedalboard.push_back(std::make_unique<OctaverPedal>(apvts.getRawParameterValue("oct_semi"), apvts.getRawParameterValue("oct_mix")));
        pedalboard.push_back(std::make_unique<CabinetPedal>());

        // 8-18: Modulation and Time (Bottom Row)
        pedalboard.push_back(std::make_unique<AutoWahPedal>(apvts.getRawParameterValue("wah_rate"), apvts.getRawParameterValue("wah_depth"), apvts.getRawParameterValue("wah_q")));
        pedalboard.push_back(std::make_unique<PhaserPedal>(apvts.getRawParameterValue("phs_rate"), apvts.getRawParameterValue("phs_depth"), apvts.getRawParameterValue("phs_freq"), apvts.getRawParameterValue("phs_feed")));
        pedalboard.push_back(std::make_unique<FlangerPedal>(apvts.getRawParameterValue("flg_rate"), apvts.getRawParameterValue("flg_depth"), apvts.getRawParameterValue("flg_feed")));
        pedalboard.push_back(std::make_unique<TremoloPedal>(apvts.getRawParameterValue("trem_depth"), apvts.getRawParameterValue("trem_rate")));
        pedalboard.push_back(std::make_unique<ChorusPedal>(apvts.getRawParameterValue("cho_rate"), apvts.getRawParameterValue("cho_depth"), apvts.getRawParameterValue("cho_mix")));
        pedalboard.push_back(std::make_unique<DelayPedal>(apvts.getRawParameterValue("delay_time"), apvts.getRawParameterValue("delay_feed"), apvts.getRawParameterValue("delay_mix")));
        pedalboard.push_back(std::make_unique<ReverbPedal>(apvts.getRawParameterValue("rvb_room"), apvts.getRawParameterValue("rvb_damp"), apvts.getRawParameterValue("rvb_mix")));
        pedalboard.push_back(std::make_unique<AcousticSimPedal>(apvts.getRawParameterValue("ac_body"), apvts.getRawParameterValue("ac_air"), apvts.getRawParameterValue("ac_reso")));
        pedalboard.push_back(std::make_unique<GuitarSynthPedal>(apvts.getRawParameterValue("syn_type"), apvts.getRawParameterValue("syn_mix"), &currentPitchHz));
        pedalboard.push_back(std::make_unique<LooperPedal>(apvts.getRawParameterValue("loop_state"), apvts.getRawParameterValue("loop_level")));
        
        // Setup Routing mapping and FORCE default state to Bypassed (Off)
        for (int i = 0; i < pedalboard.size(); ++i) {
            juce::String bypassID = "byp_" + juce::String(i);
            pedalboard[i]->isBypassed = apvts.getRawParameterValue(bypassID);
            routingMap[i].store(i);

            // Explicitly force the parameter to 1.0f (true / off) on startup
            if (auto* param = apvts.getParameter(bypassID)) {
                param->setValueNotifyingHost(1.0f);
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
        for (int i = 0; i < 18; ++i)
        {
            // Read the map to find out which pedal is currently in slot 'i'
            int pedalIndex = routingMap[i].load();
            pedalboard[pedalIndex]->process(buffer);
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
        for (int i = 0; i < 18; ++i) routingXml->setAttribute("slot" + juce::String(i), routingMap[i].load());

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
                bool isPedalUsed[18] = { false };

                // 2. Try to load the slots safely
                for (int slot = 0; slot < 18; ++slot) {
                    int pedalID = bestRouting->getIntAttribute("slot" + juce::String(slot), slot);
                    if (pedalID >= 0 && pedalID < 18 && !isPedalUsed[pedalID]) {
                        routingMap[slot].store(pedalID);
                        isPedalUsed[pedalID] = true;
                    }
                    else {
                        routingMap[slot].store(-1); // Mark slot as corrupted/empty
                    }
                }

                // 3. Heal any corrupted/missing slots
                for (int slot = 0; slot < 18; ++slot) {
                    if (routingMap[slot].load() == -1) {
                        for (int p = 0; p < 18; ++p) { // Find a pedal not yet on the board
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

        return layout;
    }
};

// DRAGGABLE RACK BUTTON
class DraggableRackButton : public juce::TextButton, public juce::DragAndDropTarget
{
public:
    int currentSlotIndex = 0;
    int fixedPedalID = 0;
    std::function<void(int, int)> onSwap; // Callback to tell the main UI to swap pedals

    DraggableRackButton(const juce::String& name, int id) : juce::TextButton(name), fixedPedalID(id) {}

    // 1. Start Dragging
    void mouseDrag(const juce::MouseEvent& e) override {
        if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
            // pass the SLOT index as a string payload
            dragContainer->startDragging(juce::String(currentSlotIndex), this);
        }
    }

    // 2. Accept a Hover
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override {
        return true;
    }

    // 3. Handle the Drop
    void itemDropped(const SourceDetails& dragSourceDetails) override {
		int draggedSlotIndex = dragSourceDetails.description.toString().getIntValue();
        if (draggedSlotIndex != currentSlotIndex && onSwap) {
            onSwap(draggedSlotIndex, currentSlotIndex); // Trigger the swap
        }
    }
};

// CUSTOM 3D GRAPHICS ENGINE
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        // Make the text box beneath the knob transparent with white text
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, const float rotaryStartAngle,
        const float rotaryEndAngle, juce::Slider& slider) override
    {
        auto radius = (float)juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = (float)x + (float)width * 0.5f;
        auto centreY = (float)y + (float)height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // 1. Drop Shadow
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(rx + 2.0f, ry + 4.0f, rw, rw);

        // 2. Metallic Base Ring 
        juce::ColourGradient baseGrad(juce::Colour(180, 180, 180), centreX, ry,
            juce::Colour(60, 60, 60), centreX, ry + rw, false);
        g.setGradientFill(baseGrad);
        g.fillEllipse(rx, ry, rw, rw);

        // 3. Dark Rubber Grip 
        auto gripRadius = radius - 3.0f;
        juce::ColourGradient gripGrad(juce::Colour(40, 40, 40), centreX, centreY - gripRadius,
            juce::Colour(10, 10, 10), centreX, centreY + gripRadius, false);
        g.setGradientFill(gripGrad);
        g.fillEllipse(centreX - gripRadius, centreY - gripRadius, gripRadius * 2.0f, gripRadius * 2.0f);

        // 4. Glowing Cyan Indicator Line
        juce::Path p;
        auto pointerLength = radius * 0.55f;
        auto pointerThickness = 3.0f;
        p.addRoundedRectangle(-pointerThickness * 0.5f, -radius + 5.0f, pointerThickness, pointerLength, 1.5f);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));

        g.setColour(juce::Colours::cyan);
        g.fillPath(p);

        // 5. LED Bloom/Glow on the tip
        g.setColour(juce::Colours::cyan.withAlpha(0.4f));
        g.fillEllipse(centreX + std::sin(angle) * (radius - 9.0f) - 4.0f,
            centreY - std::cos(angle) * (radius - 9.0f) - 4.0f, 8.0f, 8.0f);
    }
};


// DYNAMIC UI FACTORY COMPONENT
class PedalUIBlock : public juce::Component
{
public:
    PedalUIBlock(juce::AudioProcessorValueTreeState& state,
        const juce::StringArray& sIDs,
        const juce::String& cID = "",
        const juce::StringArray& comboItems = {})
        : apvts(state), sliderIDs(sIDs), comboID(cID)
    {
        // 1. Generate Sliders dynamically
        for (auto& id : sliderIDs) {
            auto* slider = new juce::Slider();
            slider->setSliderStyle(juce::Slider::Rotary);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
            addAndMakeVisible(slider);
            sliders.add(slider); // OwnedArray takes ownership of the pointer

            auto* attachment = new juce::AudioProcessorValueTreeState::SliderAttachment(apvts, id, *slider);
            sliderAttachments.add(attachment);
        }

        // 2. Generate ComboBox dynamically (if a comboID was provided)
        if (comboID.isNotEmpty()) {
            comboBox.addItemList(comboItems, 1);
            addAndMakeVisible(comboBox);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, comboID, comboBox);
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(5); // Shrink slightly to leave room for the shadow

        // 1. Enclosure Drop Shadow
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(area.translated(4, 5).toFloat(), 10.0f);

        // 2. Dark Metallic Chassis 
        juce::ColourGradient pedalGrad(juce::Colour(60, 65, 70), 0, 0,
            juce::Colour(20, 22, 25), 0, (float)area.getHeight(), false);
        g.setGradientFill(pedalGrad);
        g.fillRoundedRectangle(area.toFloat(), 10.0f);

        // 3. Inner Chrome Lip
        g.setColour(juce::Colours::grey.withAlpha(0.3f));
        g.drawRoundedRectangle(area.toFloat(), 10.0f, 1.5f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.0f, juce::Font::bold)); // Bolder, cleaner font

        // Edge Case: Cabinet Simulator
        if (sliderIDs.isEmpty() && comboID.isEmpty()) {
            g.drawText("Cabinet IR Loaded. No parameters to edit.", 20, 20, 400, 20, juce::Justification::centredLeft);
            return;
        }

        // Draw Labels
        int x = 0;
        for (auto& id : sliderIDs) {
            juce::String name = apvts.getParameter(id)->getName(100);
            g.drawText(name, x, 10, 100, 20, juce::Justification::centred);
            x += 130;
        }
        if (comboID.isNotEmpty()) {
            juce::String name = apvts.getParameter(comboID)->getName(100);
            g.drawText(name, x, 10, 100, 20, juce::Justification::centred);
        }
    }

    void resized() override
    {
        int x = 0;
        for (auto* slider : sliders) {
            slider->setBounds(x, 30, 100, 100);
            x += 130;
        }
        if (comboID.isNotEmpty()) {
            comboBox.setBounds(x, 60, 100, 30);
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray sliderIDs;
    juce::String comboID;

    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;

    juce::ComboBox comboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
};

// CUSTOM LOOPER UI BLOCK
class LooperUIBlock : public juce::Component, public juce::Timer
{
public:
    LooperUIBlock(juce::AudioProcessorValueTreeState& state) : apvts(state)
    {
        // 1. Setup Custom Transport Buttons
        setupButton(btnStop, "STOP", juce::Colours::darkgrey);
        setupButton(btnRec, "REC", juce::Colours::darkred);
        setupButton(btnPlay, "PLAY", juce::Colours::darkgreen);
        setupButton(btnDub, "DUB", juce::Colours::orange);

        // 2. Setup Loop Volume Knob
        levelKnob.setSliderStyle(juce::Slider::Rotary);
        levelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        addAndMakeVisible(levelKnob);
        levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "loop_level", levelKnob);

        startTimerHz(15); // Start the LED checking timer
    }

    void setupButton(juce::TextButton& btn, const juce::String& text, juce::Colour baseColor)
    {
        btn.setButtonText(text);
        btn.setColour(juce::TextButton::buttonColourId, baseColor.withAlpha(0.3f)); // Dim when off
        addAndMakeVisible(btn);

        // When clicked, safely write the new state integer into the APVTS database
        btn.onClick = [this, &btn, text] {
            int stateVal = 0;
            if (text == "REC") stateVal = 1;
            else if (text == "PLAY") stateVal = 2;
            else if (text == "DUB") stateVal = 3;

            // Convert the integer (0-3) into the 0.0-1.0 float format the DAW expects
            if (auto* p = apvts.getParameter("loop_state")) {
                p->setValueNotifyingHost(p->convertTo0to1((float)stateVal));
            }
            };
    }

    void timerCallback() override
    {
        // Read the current state from the Audio Thread
        int currentState = static_cast<int>(apvts.getRawParameterValue("loop_state")->load());

        // 1. Reset all buttons to dim background with WHITE text
        btnStop.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey.withAlpha(0.3f));
        btnStop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

        btnRec.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred.withAlpha(0.3f));
		btnRec.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

        btnPlay.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgreen.withAlpha(0.3f));
		btnPlay.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

        btnDub.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.withAlpha(0.3f));
        btnDub.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

        // 2. Illuminate active state
        if (currentState == 0) {
            btnStop.setColour(juce::TextButton::buttonColourId, juce::Colours::white);
            btnStop.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        }
        else if (currentState == 1) {
            btnRec.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
            btnRec.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        } 
        else if (currentState == 2) {
            btnPlay.setColour(juce::TextButton::buttonColourId, juce::Colours::limegreen);
			btnPlay.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        }
        else if (currentState == 3) {
            btnDub.setColour(juce::TextButton::buttonColourId, juce::Colours::yellow);
            btnDub.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
        }
    }

    void paint(juce::Graphics& g) override
    {
        // Draw the exact same physical pedal chassis as the standard blocks
        auto area = getLocalBounds().reduced(5);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(area.translated(4, 5).toFloat(), 10.0f);

        juce::ColourGradient pedalGrad(juce::Colour(60, 65, 70), 0, 0,
            juce::Colour(20, 22, 25), 0, (float)area.getHeight(), false);
        g.setGradientFill(pedalGrad);
        g.fillRoundedRectangle(area.toFloat(), 10.0f);

        g.setColour(juce::Colours::grey.withAlpha(0.3f));
        g.drawRoundedRectangle(area.toFloat(), 10.0f, 1.5f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText("Loop Vol", 430, 10, 100, 20, juce::Justification::centred);
    }

    void resized() override
    {
        // Draw big hardware transport buttons
        btnStop.setBounds(20, 40, 80, 60);
        btnRec.setBounds(110, 40, 80, 60);
        btnPlay.setBounds(200, 40, 80, 60);
        btnDub.setBounds(290, 40, 80, 60);

        levelKnob.setBounds(430, 30, 100, 100);
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::TextButton btnStop, btnRec, btnPlay, btnDub;
    juce::Slider levelKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAttach;
};

// THE MAIN EDITOR
class MyAmpSimEditor : public juce::AudioProcessorEditor, public juce::Timer, public juce::DragAndDropContainer
{
public:
    MyAmpSimEditor(MyAmpSimAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
    {
        setSize(1000, 380);

        setLookAndFeel(&customLaf);

        // SETUP BACKGROUND LOADER
        addAndMakeVisible(btnBg);
        btnBg.setColour(juce::TextButton::buttonColourId, juce::Colours::darkslateblue);
        btnBg.setTooltip("Load Custom Background");

        // SETUP BACKGROUND CLEAR BUTTON
        addChildComponent(btnClearBg); // addChildComponent keeps it hidden by default
        btnClearBg.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        btnClearBg.setTooltip("Clear Background");

        btnClearBg.onClick = [this] {
            backgroundImage = juce::Image(); // Nullify the image
            btnClearBg.setVisible(false);    // Hide the button again
            repaint();
            };

        btnBg.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>("Select Background", juce::File::getSpecialLocation(juce::File::userPicturesDirectory), "*.jpg;*.jpeg;*.png");
            fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    if (fc.getResult().existsAsFile()) {
                        backgroundImage = juce::ImageCache::getFromFile(fc.getResult());
                        btnClearBg.setVisible(true); // Reveal the clear button
                        repaint();
                    }
                });
            };

        // 1. SETUP TOP RACK BUTTONS
        juce::StringArray pedalNames = {
            "Gate", "Comp", "Boost", "Dist", "EQ", "Pitch", "Oct", "Cab",
            "Wah", "Phas", "Flng", "Trem", "Cho", "Dly", "Rvb", "Acoust", "Synth", "Loop"
        };

        for (int i = 0; i < 18; ++i) {
            auto* btn = new DraggableRackButton(pedalNames[i], i);
            btn->currentSlotIndex = i; // Initially the slot matches ID

            // Show its parameters when clicked
            btn->onClick = [this, btn] {
                currentSelectedPedalID = btn->fixedPedalID;
                updateVisibilities();
                repaint();
                };

			// Swap the routing when dragged and dropped
            btn->onSwap = [this](int fromSlot, int toSlot) {
                swapRouting(fromSlot, toSlot);
                };

            addAndMakeVisible(btn);
            rackButtons.add(btn);
        }

        // 2. SETUP GLOBAL UI CONTROLS
        bypassToggle.setButtonText("PEDAL OFF");
        addAndMakeVisible(bypassToggle);

        tunerToggle.setButtonText("SHOW TUNER");
        tunerToggle.onClick = [this] { repaint(); };
        addAndMakeVisible(tunerToggle);

        killAllButton.setButtonText("RESET ALL SETTINGS");
        killAllButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
        killAllButton.onClick = [this] {
            for (auto* param : audioProcessor.getParameters()) {
                if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*>(param))
                    rangedParam->setValueNotifyingHost(rangedParam->getDefaultValue());
            }
            };
        addAndMakeVisible(killAllButton);

        // --- SAVE PRESET ---
        addAndMakeVisible(btnSave);
        btnSave.setColour(juce::TextButton::buttonColourId, juce::Colours::darkorange);
        btnSave.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>("Save Preset", juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.xml");

            fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    juce::File file = fc.getResult();
                    if (file != juce::File{}) {
                        auto state = audioProcessor.apvts.copyState();
                        std::unique_ptr<juce::XmlElement> xml(state.createXml());

                        // SCORCHED EARTH POLICY
                        xml->deleteAllChildElementsWithTagName("ROUTING");

                        auto* routingXml = new juce::XmlElement("ROUTING");
                        for (int i = 0; i < 18; ++i) routingXml->setAttribute("slot" + juce::String(i), audioProcessor.routingMap[i].load());
                        xml->addChildElement(routingXml);
                        xml->writeTo(file);
                    }
                });
            };

        // --- LOAD PRESET (Armored Version) ---
        addAndMakeVisible(btnLoad);
        btnLoad.setColour(juce::TextButton::buttonColourId, juce::Colours::darkorange);
        btnLoad.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>("Load Preset", juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.xml");

            fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    juce::File file = fc.getResult();
                    if (file.existsAsFile()) {
                        std::unique_ptr<juce::XmlElement> xmlState = juce::XmlDocument::parse(file);
                        if (xmlState != nullptr) {

                            // 1. Find the LAST routing tag
                            juce::XmlElement* bestRouting = nullptr;
                            for (auto* child : xmlState->getChildIterator()) {
                                if (child->hasTagName("ROUTING")) bestRouting = child;
                            }

                            if (bestRouting != nullptr) {
                                for (int slot = 0; slot < 18; ++slot) {
                                    audioProcessor.routingMap[slot].store(bestRouting->getIntAttribute("slot" + juce::String(slot), slot));
                                }
                            }

                            // 2. Scrub XML and Load Knobs
                            xmlState->deleteAllChildElementsWithTagName("ROUTING");
                            audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

                            // 3. UI IMMUNE SYSTEM: Guarantee 1-to-1 button mapping
                            bool slotFilled[18] = { false };
                            for (auto* btn : rackButtons) {
                                int targetSlot = -1;

                                // Look for where the Audio Engine placed this pedal
                                for (int s = 0; s < 18; ++s) {
                                    if (audioProcessor.routingMap[s].load() == btn->fixedPedalID && !slotFilled[s]) {
                                        targetSlot = s;
                                        break;
                                    }
                                }

                                // If the XML was hopelessly corrupt, shove the button in the first empty slot to save the UI
                                if (targetSlot == -1) {
                                    for (int s = 0; s < 18; ++s) {
                                        if (!slotFilled[s]) { targetSlot = s; break; }
                                    }
                                }

                                btn->currentSlotIndex = targetSlot;
                                slotFilled[targetSlot] = true;
                            }

                            resized();
                            repaint();
                        }
                    }
                });
            };

        // Setup Master Vol
        masterVol.setSliderStyle(juce::Slider::Rotary);
        masterVol.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        addAndMakeVisible(masterVol);
        volAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "master_vol", masterVol);

        // 3. GENERATE PEDAL BLOCKS USING THE FACTORY
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "ng_thresh", "ng_ratio", "ng_att", "ng_rel" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "cmp_thresh", "cmp_ratio", "cmp_att", "cmp_rel" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "bst_gain" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "drive" }, "dist_type", { "Tube", "Overdrive", "Fuzz" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "eq_low", "eq_mid", "eq_high" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "ps_semi", "ps_mix" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "oct_semi", "oct_mix" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, {})); // Cabinet
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "wah_rate", "wah_depth", "wah_q" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "phs_rate", "phs_depth", "phs_freq", "phs_feed" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "flg_rate", "flg_depth", "flg_feed" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "trem_depth", "trem_rate" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "cho_rate", "cho_depth", "cho_mix" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "delay_time", "delay_feed", "delay_mix" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "rvb_room", "rvb_damp", "rvb_mix" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "ac_body", "ac_air", "ac_reso" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "syn_mix" }, "syn_type", { "Sine", "Square", "Saw" }));
        pedalBlocks.add(new LooperUIBlock(p.apvts));

        // Add them to the UI but keep them hidden initially
        for (auto* block : pedalBlocks) addChildComponent(block);

        updateVisibilities();
        startTimerHz(30);
    }

    ~MyAmpSimEditor() override
    {
        setLookAndFeel(nullptr);
    }

    void swapRouting(int slotA, int slotB)
    {
        // 1. Swap the internal audio processor routing map (thread safe bc exceptions bit my ass last time)
        int dspPedalA = audioProcessor.routingMap[slotA].load();
        int dspPedalB = audioProcessor.routingMap[slotB].load();
        audioProcessor.routingMap[slotA].store(dspPedalB);
        audioProcessor.routingMap[slotB].store(dspPedalA);

        // 2. Find the two UI buttons sitting in those slots and swap their slot assignments
        for (auto* btn : rackButtons) {
            if (btn->currentSlotIndex == slotA) btn->currentSlotIndex = slotB;
            else if (btn->currentSlotIndex == slotB) btn->currentSlotIndex = slotA;
        }

        // 3. Force the UI to visually reorganize itself based on the new slots
        resized();
    }

    void timerCallback() override
    {
        bool needsRepaint = false;

        for (int i = 0; i < 18; ++i) {
            auto* paramPtr = audioProcessor.apvts.getRawParameterValue("byp_" + juce::String(i));
            if (paramPtr != nullptr) {
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

        float peak = audioProcessor.outputPeak.load();
        // Smooth logarithmic decay for the volume meter
        meterLevel = meterLevel * 0.85f + peak * 0.15f;
        if (meterLevel > 0.005f || peak > 0.005f) needsRepaint = true;

        if (needsRepaint) repaint();
    }

    void updateVisibilities()
    {
        // Only show the block matching the currently clicked pedal ID
        for (int i = 0; i < pedalBlocks.size(); ++i) {
            pedalBlocks[i]->setVisible(i == currentSelectedPedalID);
        }

        // Dynamically re-bind the Bypass switch to the current pedal
        juce::String bypassID = "byp_" + juce::String(currentSelectedPedalID);
        bypassAttachment.reset();
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, bypassID, bypassToggle);

        resized();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRect(getLocalBounds().removeFromTop(80));
        g.setColour(juce::Colours::darkred);
        g.drawLine(0, 40, 1000, 40, 2.0f);

        // Draw Custom Image OR Dark Studio Gradient
        if (backgroundImage.isValid()) {
            g.drawImage(backgroundImage, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
        }
        else {
            juce::ColourGradient bgGrad(juce::Colour(35, 40, 45), 0, 0,
                juce::Colour(10, 12, 15), 0, (float)getHeight(), false);
            g.setGradientFill(bgGrad);
            g.fillAll();
        }

        // Draw Top Rack Shadow/Background
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(0, 0, 1000, 85);
        g.setColour(juce::Colours::darkred);
        g.drawLine(0, 40, 1000, 40, 2.0f);
        g.drawLine(0, 85, 1000, 85, 3.0f);

        // Give the rack buttons a uniform, dark hardware look. Highlight blue if selected.
        for (auto* btn : rackButtons) {
            if (currentSelectedPedalID == btn->fixedPedalID) {
                btn->setColour(juce::TextButton::buttonColourId, juce::Colours::dodgerblue.withAlpha(0.5f));
            }
            else {
                btn->setColour(juce::TextButton::buttonColourId, juce::Colours::black.withAlpha(0.3f));
            }
        }

        // TUNER SCREEN
        if (tunerToggle.getToggleState())
        {
            g.setColour(juce::Colours::black);
            g.fillRect(700, 150, 130, 80);
            g.setColour(juce::Colours::limegreen);

            juce::String noteName = "--";
            if (lastHz > 40.0f) {
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
        g.drawText("Master Vol", 850, 140, 100, 20, juce::Justification::centred);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
		// glowing LEDs for active pedals in the rack
        for (auto* btn : rackButtons) {
            bool isBypassed = pedalStates[btn->fixedPedalID];
            juce::Colour ledColor = isBypassed ? juce::Colours::darkred.withAlpha(0.3f) : juce::Colours::limegreen;

            // Get the button's exact coordinates and place the LED in the top-left corner
            auto bounds = btn->getBounds();
            float ledX = (float)bounds.getX() + 6.0f;
            float ledY = (float)bounds.getY() + 6.0f;

            // Draw Bloom/Glow if Active
            if (!isBypassed) {
                g.setColour(ledColor.withAlpha(0.4f));
                g.fillEllipse(ledX - 3.0f, ledY - 3.0f, 14.0f, 14.0f);
            }
            // Draw Solid Core
            g.setColour(ledColor);
            g.fillEllipse(ledX, ledY, 8.0f, 8.0f);
        }

		// master volume meter (simple vertical bar on the right)
        juce::Rectangle<float> meterBounds(960.0f, 170.0f, 15.0f, 100.0f);

        // Background track
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.fillRoundedRectangle(meterBounds, 3.0f);

        // Calculate height based on volume (Capped at 1.2 for headroom visual)
        float levelHeight = juce::jmin(meterLevel, 1.2f) / 1.2f * meterBounds.getHeight();
        juce::Rectangle<float> fillBounds = meterBounds.withTrimmedTop(meterBounds.getHeight() - levelHeight);

        // Gradient: Green -> Yellow -> Red (Clipping)
        juce::ColourGradient meterGrad(juce::Colours::red, meterBounds.getX(), meterBounds.getY(),
            juce::Colours::limegreen, meterBounds.getX(), meterBounds.getBottom(), false);
        meterGrad.addColour(0.3, juce::Colours::yellow);

        g.setGradientFill(meterGrad);
        g.fillRoundedRectangle(fillBounds, 3.0f);

        // Draw Chrome bezel around meter
        g.setColour(juce::Colours::grey.withAlpha(0.4f));
        g.drawRoundedRectangle(meterBounds, 3.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto rackTop = area.removeFromTop(40);
        auto rackBottom = area.removeFromTop(40);

        // Dynamic button positioning
        int topWidth = 1000 / 9;
        int botWidth = 1000 / 9;

        for (int slot = 0; slot < 18; ++slot)
        {
            DraggableRackButton* btnToDraw = nullptr;
            for (auto* btn : rackButtons) {
                if (btn->currentSlotIndex == slot) { btnToDraw = btn; break; }
            }

            if (btnToDraw) {
                // Wrap the coordinates in a juce::Rectangle first, shrink it, then set the bounds!
                if (slot < 9) {
                    btnToDraw->setBounds(juce::Rectangle<int>(slot * topWidth, 0, topWidth, 40).reduced(2));
                }
                else {
                    btnToDraw->setBounds(juce::Rectangle<int>((slot - 9) * botWidth, 40, botWidth, 40).reduced(2));
                }
            }
        }

        bypassToggle.setBounds(50, 100, 150, 30);
        tunerToggle.setBounds(700, 100, 120, 30);
        killAllButton.setBounds(850, 100, 150, 30);

        bypassToggle.setBounds(20, 100, 100, 30);
        tunerToggle.setBounds(130, 100, 100, 30);

        btnSave.setBounds(600, 100, 100, 30);
        btnLoad.setBounds(710, 100, 100, 30);
        killAllButton.setBounds(820, 100, 150, 30);

        // Place the active PedalUIBlock
        for (auto* block : pedalBlocks) {
            block->setBounds(50, 140, 600, 150);
        }

        masterVol.setBounds(850, 170, 100, 100);
        btnClearBg.setBounds(960, 305, 30, 30);
        btnBg.setBounds(960, 340, 30, 30);
    }

    juce::TextButton btnSave{ "SAVE PRESET" }, btnLoad{ "LOAD PRESET" };
    std::unique_ptr<juce::FileChooser> fileChooser; // JUCE's native file explorer window
    
    CustomLookAndFeel customLaf;
    juce::Image backgroundImage;
    juce::TextButton btnBg{ "BG" }, btnClearBg{ "X" };

private:
    MyAmpSimAudioProcessor& audioProcessor;
    int currentSelectedPedalID = 0; // Tracks which pedal's knobs are visible
    float lastHz = 0.0f;
    float meterLevel = 0.0f; // Smooths out the meter animation
    bool pedalStates[18] = { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true };

    juce::OwnedArray<DraggableRackButton> rackButtons; 
    juce::OwnedArray<juce::Component> pedalBlocks;

    juce::TextButton killAllButton;
    juce::ToggleButton bypassToggle, tunerToggle;
    juce::Slider masterVol;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
};

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }