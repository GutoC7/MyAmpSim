#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CustomUI.h"

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
            "Wah", "Phas", "Flng", "Trem", "Cho", "Dly", "Rvb", "Acoust", "Synth", "Loop", "Crush", "Ring"
        };

        for (int i = 0; i < 20; ++i) {
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
                        for (int i = 0; i < 20; ++i) routingXml->setAttribute("slot" + juce::String(i), audioProcessor.routingMap[i].load());
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
                                for (int slot = 0; slot < 20; ++slot) {
                                    audioProcessor.routingMap[slot].store(bestRouting->getIntAttribute("slot" + juce::String(slot), slot));
                                }
                            }

                            // 2. Scrub XML and Load Knobs
                            xmlState->deleteAllChildElementsWithTagName("ROUTING");
                            audioProcessor.apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

                            // 3. UI IMMUNE SYSTEM: Guarantee 1-to-1 button mapping
                            bool slotFilled[20] = { false };
                            for (auto* btn : rackButtons) {
                                int targetSlot = -1;

                                // Look for where the Audio Engine placed this pedal
                                for (int s = 0; s < 20; ++s) {
                                    if (audioProcessor.routingMap[s].load() == btn->fixedPedalID && !slotFilled[s]) {
                                        targetSlot = s;
                                        break;
                                    }
                                }

                                // If the XML was hopelessly corrupt, shove the button in the first empty slot to save the UI
                                if (targetSlot == -1) {
                                    for (int s = 0; s < 20; ++s) {
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
        pedalBlocks.add(new CabinetUIBlock([this](const juce::File& file) {
            audioProcessor.loadCabinetIR(file);
            }));
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
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "bc_bits", "bc_down" }));
        pedalBlocks.add(new PedalUIBlock(p.apvts, { "rm_freq", "rm_mix" }));

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

        for (int i = 0; i < 20; ++i) {
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
        int topWidth = 1000 / 10;
        int botWidth = 1000 / 10;

        for (int slot = 0; slot < 20; ++slot)
        {
            DraggableRackButton* btnToDraw = nullptr;
            for (auto* btn : rackButtons) {
                if (btn->currentSlotIndex == slot) { btnToDraw = btn; break; }
            }

            if (btnToDraw) {
                // Wrap the coordinates in a juce::Rectangle first, shrink it, then set the bounds!
                if (slot < 10) {
                    btnToDraw->setBounds(juce::Rectangle<int>(slot * topWidth, 0, topWidth, 40).reduced(2));
                }
                else {
                    btnToDraw->setBounds(juce::Rectangle<int>((slot - 10) * botWidth, 40, botWidth, 40).reduced(2));
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
    bool pedalStates[20] = {
        true, true, true, true, true,
        true, true, true, true, true,
        true, true, true, true, true,
        true, true, true, true, true
    };

    juce::OwnedArray<DraggableRackButton> rackButtons;
    juce::OwnedArray<juce::Component> pedalBlocks;

    juce::TextButton killAllButton;
    juce::ToggleButton bypassToggle, tunerToggle;
    juce::Slider masterVol;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
};