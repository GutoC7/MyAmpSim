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
        g.setColour(juce::Colours::white);
        g.setFont(20.0f);

        // Edge Case: Cabinet Simulator has no parameters
        if (sliderIDs.isEmpty() && comboID.isEmpty()) {
            g.drawText("Cabinet IR Loaded. No parameters to edit.", 0, 10, 400, 20, juce::Justification::centredLeft);
            return;
        }

        // Draw Labels dynamically by asking the APVTS for the parameter's real name
        int x = 0;
        for (auto& id : sliderIDs) {
            juce::String name = apvts.getParameter(id)->getName(100);
            g.drawText(name, x, 0, 100, 20, juce::Justification::centred);
            x += 130; // Shift right for the next label
        }
        if (comboID.isNotEmpty()) {
            juce::String name = apvts.getParameter(comboID)->getName(100);
            g.drawText(name, x, 0, 100, 20, juce::Justification::centred);
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

// THE MAIN EDITOR
class MyAmpSimEditor : public juce::AudioProcessorEditor, public juce::Timer, public juce::DragAndDropContainer
{
public:
    MyAmpSimEditor(MyAmpSimAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
    {
        setSize(1000, 380);

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
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "loop_level" }, "loop_state", { "Stop", "Record", "Play", "Dub" }));

        // Add them to the UI but keep them hidden initially
        for (auto* block : pedalBlocks) addChildComponent(block);

        updateVisibilities();
        startTimerHz(30);
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

        // Update color logic to use fixedPedalID
        for (auto* btn : rackButtons) {
            if (currentSelectedPedalID == btn->fixedPedalID) {
                btn->setColour(juce::TextButton::buttonColourId, juce::Colours::dodgerblue);
            }
            else {
                bool isBypassed = pedalStates[btn->fixedPedalID];
                btn->setColour(juce::TextButton::buttonColourId, isBypassed ? juce::Colours::darkgrey : juce::Colours::limegreen);
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

    void resized() override
    {
        auto area = getLocalBounds();
        auto rackTop = area.removeFromTop(40);
        auto rackBottom = area.removeFromTop(40);

        // DYNAMIC BUTTON POSITIONING (Absolute Math Version)
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
    }

    juce::TextButton btnSave{ "SAVE PRESET" }, btnLoad{ "LOAD PRESET" };
    std::unique_ptr<juce::FileChooser> fileChooser; // JUCE's native file explorer window

private:
    MyAmpSimAudioProcessor& audioProcessor;
    int currentSelectedPedalID = 0; // Tracks which pedal's knobs are visible
    float lastHz = 0.0f;
    bool pedalStates[18] = { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true };

    juce::OwnedArray<DraggableRackButton> rackButtons; 
    juce::OwnedArray<PedalUIBlock> pedalBlocks;

    juce::TextButton killAllButton;
    juce::ToggleButton bypassToggle, tunerToggle;
    juce::Slider masterVol;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
};

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }