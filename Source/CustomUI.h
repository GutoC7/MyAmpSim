#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

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
    PedalUIBlock(MyAmpSimAudioProcessor& p,
        const juce::StringArray& sIDs,
        const juce::String& cID = "",
        const juce::StringArray& comboItems = {})
        : processor(p), sliderIDs(sIDs), comboID(cID)
    {
        // 1. Generate Sliders dynamically
        for (auto& id : sliderIDs) {
            auto* slider = new juce::Slider();
            slider->setSliderStyle(juce::Slider::Rotary);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);

            // --- THE MIDI LEARN TRIGGER ---
            slider->onDragStart = [this, id] {
                if (processor.isMidiLearnActive.load()) {
                    int lastCC = processor.lastMovedCC.load();
                    if (lastCC >= 0) processor.bindCC(lastCC, id); // Bind it!
                }
                };

            addAndMakeVisible(slider);
            sliders.add(slider);

            // Attach securely to the audio processor database
            sliderAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, id, *slider));
        }

        // 2. Generate ComboBox dynamically (if a comboID was provided)
        if (comboID.isNotEmpty()) {
            comboBox.addItemList(comboItems, 1);
            addAndMakeVisible(comboBox);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, comboID, comboBox);
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(5);

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
        g.setFont(juce::Font(16.0f, juce::Font::bold));

        // Edge Case: Cabinet Simulator
        if (sliderIDs.isEmpty() && comboID.isEmpty()) {
            g.drawText("Cabinet IR Loaded. No parameters to edit.", 20, 20, getWidth() - 40, 20, juce::Justification::centredLeft);
            return;
        }

        // 4. Draw Labels (DYNAMICALLY SPACED)
        int numItems = sliderIDs.size() + (comboID.isNotEmpty() ? 1 : 0);
        if (numItems > 0) {
            int itemWidth = getWidth() / numItems;
            int x = 0;

            for (auto& id : sliderIDs) {
                juce::String name = processor.apvts.getParameter(id)->getName(100);
                g.drawText(name, x, 15, itemWidth, 20, juce::Justification::centred);
                x += itemWidth;
            }
            if (comboID.isNotEmpty()) {
                juce::String name = processor.apvts.getParameter(comboID)->getName(100);
                g.drawText(name, x, 15, itemWidth, 20, juce::Justification::centred);
            }
        }
    }

    void resized() override
    {
        int numItems = sliders.size() + (comboID.isNotEmpty() ? 1 : 0);
        if (numItems == 0) return;

        int itemWidth = getWidth() / numItems;

        // Dynamically scale the knob size based on the window, but cap it at 120px
        int knobSize = juce::jmin(itemWidth - 20, getHeight() - 60);
        knobSize = juce::jmin(knobSize, 120);

        // Center the knobs vertically inside the chassis
        int yOffset = 35 + (getHeight() - 40 - knobSize) / 2;

        int x = 0;
        for (auto* slider : sliders) {
            slider->setBounds(x + (itemWidth - knobSize) / 2, yOffset, knobSize, knobSize);
            x += itemWidth;
        }

        if (comboID.isNotEmpty()) {
            comboBox.setBounds(x + (itemWidth - 100) / 2, yOffset + (knobSize / 2) - 15, 100, 30);
        }
    }

private:
    MyAmpSimAudioProcessor& processor;
    juce::StringArray sliderIDs;
    juce::String comboID;

    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;

    juce::ComboBox comboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
};

// CUSTOM CABINET UI BLOCK
class CabinetUIBlock : public juce::Component
{
public:
    CabinetUIBlock(std::function<void(const juce::File&)> onLoadFile, std::function<void(int)> onSelectFactory)
        : loadCallback(onLoadFile), factoryCallback(onSelectFactory)
    {
        // 1. The Custom User File Button
        addAndMakeVisible(btnLoadIR);
        btnLoadIR.setButtonText("LOAD CUSTOM .WAV");
        btnLoadIR.setColour(juce::TextButton::buttonColourId, juce::Colours::darkorange);

        btnLoadIR.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>("Select IR (.wav)", juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.wav");
            fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    if (fc.getResult().existsAsFile()) {
                        irName = fc.getResult().getFileName();
                        if (loadCallback) loadCallback(fc.getResult());
                        repaint();
                    }
                });
            };

        // 2. The Built-in Factory Dropdown
        addAndMakeVisible(factorySelector);
        factorySelector.setTextWhenNothingSelected("Select Factory Cabinet...");

        // DYNAMIC UI: Automatically populate the menu using the embedded filenames
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
            // JUCE ComboBox IDs must start at 1, so we use i + 1
            factorySelector.addItem(BinaryData::originalFilenames[i], i + 1);
        }

        factorySelector.onChange = [this] {
            // Convert the ComboBox ID (starts at 1) back to the Array Index (starts at 0)
            int selectedIndex = factorySelector.getSelectedId() - 1;

            if (factoryCallback) factoryCallback(selectedIndex);
            irName = "Factory: " + factorySelector.getText();
            repaint();
            };
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(5);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(area.translated(4, 5).toFloat(), 10.0f);

        juce::ColourGradient pedalGrad(juce::Colour(60, 65, 70), 0, 0, juce::Colour(20, 22, 25), 0, (float)area.getHeight(), false);
        g.setGradientFill(pedalGrad);
        g.fillRoundedRectangle(area.toFloat(), 10.0f);
        g.setColour(juce::Colours::grey.withAlpha(0.3f));
        g.drawRoundedRectangle(area.toFloat(), 10.0f, 1.5f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("Cabinet Impulse Response", 20, 20, 300, 30, juce::Justification::centredLeft);

        g.setFont(juce::Font(14.0f, juce::Font::italic));
        g.setColour(juce::Colours::cyan);
        g.drawText(irName.isEmpty() ? "No IR Loaded (Bypassed)" : "Loaded: " + irName, 20, 60, 400, 30, juce::Justification::centredLeft);
    }

    void resized() override
    {
        factorySelector.setBounds(20, 100, 250, 30);
        btnLoadIR.setBounds(400, 95, 160, 40);
    }

private:
    juce::TextButton btnLoadIR;
    juce::ComboBox factorySelector;
    juce::String irName;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::function<void(const juce::File&)> loadCallback;
    std::function<void(int)> factoryCallback;
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

// PRESET DRAWER (SLIDE-OUT BROWSER)
class PresetDrawer : public juce::Component, public juce::ListBoxModel
{
public:
    PresetDrawer(juce::File directory, std::function<void(const juce::File&)> onLoadFile)
        : presetDir(directory), loadCallback(onLoadFile)
    {
        addAndMakeVisible(presetList);
        presetList.setModel(this);
        presetList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        presetList.setRowHeight(35);

        refreshPresets();
    }

    void refreshPresets()
    {
        // Scan the directory for all .xml files and populate the array
        presetFiles = presetDir.findChildFiles(juce::File::findFiles, false, "*.xml");
        presetList.updateContent();
    }

    // --- ListBoxModel Overrides ---
    int getNumRows() override { return presetFiles.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected) g.fillAll(juce::Colours::dodgerblue.withAlpha(0.4f));

        g.setColour(juce::Colours::white);
        g.setFont(16.0f);

        // Draw the filename without the .xml extension
        juce::String presetName = presetFiles[rowNumber].getFileNameWithoutExtension();
        g.drawText(presetName, 15, 0, width - 20, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (loadCallback) {
            loadCallback(presetFiles[row]); // Silently load the preset!
        }
    }

    void paint(juce::Graphics& g) override
    {
        // Draw a dark, semi-transparent glass panel background
        g.fillAll(juce::Colours::black.withAlpha(0.85f));
        g.setColour(juce::Colours::darkgrey);
        g.drawRect(getLocalBounds(), 2.0f);

        g.setColour(juce::Colours::cyan);
        g.setFont(juce::Font(20.0f, juce::Font::bold));
        g.drawText("PRESET LIBRARY", 0, 10, getWidth(), 30, juce::Justification::centred);
        g.drawLine(20, 45, getWidth() - 20, 45, 1.0f);
    }

    void resized() override
    {
        presetList.setBounds(10, 55, getWidth() - 20, getHeight() - 65);
    }

private:
    juce::ListBox presetList;
    juce::File presetDir;
    juce::Array<juce::File> presetFiles;
    std::function<void(const juce::File&)> loadCallback;
};

// CUSTOM EQ UI BLOCK (FFT + OSCILLOSCOPE)
class EqUIBlock : public juce::Component, public juce::Timer
{
public:
    EqUIBlock(MyAmpSimAudioProcessor& p) : processor(p)
    {
        juce::StringArray ids = { "eq_low", "eq_mid", "eq_high" };
        for (auto& id : ids) {
            auto* slider = new juce::Slider();
            slider->setSliderStyle(juce::Slider::Rotary);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
            addAndMakeVisible(slider);
            sliders.add(slider);
            sliderAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, id, *slider));
        }
        startTimerHz(30); // 30 FPS Graphics
    }

    void timerCallback() override
    {
        if (processor.nextFFTBlockReady) {
            drawNextFrameOfSpectrum();
            processor.nextFFTBlockReady = false;
            repaint();
        }
    }

    void drawNextFrameOfSpectrum()
    {
        auto bounds = getLocalBounds().reduced(5);
        juce::Rectangle<float> screenBounds(20.0f, 20.0f, (float)bounds.getWidth() - 40.0f, (float)bounds.getHeight() - 110.0f);

        // Split the screen 
        juce::Rectangle<float> fftBounds = screenBounds.removeFromTop(screenBounds.getHeight() * 0.5f);
        juce::Rectangle<float> waveBounds = screenBounds;

        // --- 1. BUILD THE TIME-DOMAIN OSCILLOSCOPE WAVE ---
        juce::Path newWavePath;
        bool firstWave = true;
        for (int i = 0; i < processor.fftSize; ++i) {
            float x = waveBounds.getX() + ((float)i / (float)processor.fftSize) * waveBounds.getWidth();
            // Raw audio ranges from -1.0 to 1.0. Map it to the screen height.
            float y = juce::jmap(processor.waveData[i], -1.2f, 1.2f, waveBounds.getBottom(), waveBounds.getY());
            y = juce::jlimit(waveBounds.getY(), waveBounds.getBottom(), y); // Prevent clipping outside the glass

            if (firstWave) {
                newWavePath.startNewSubPath(x, y);
                firstWave = false;
            }
            else {
                newWavePath.lineTo(x, y);
            }
        }
        wavePath = newWavePath;

        // --- 2. BUILD THE FREQUENCY-DOMAIN FFT GRAPH ---
        processor.window.multiplyWithWindowingTable(processor.fftData, processor.fftSize);
        processor.forwardFFT.performFrequencyOnlyForwardTransform(processor.fftData);

        juce::Path newFftPath;
        int numBins = processor.fftSize / 2;
        bool firstFft = true;
        float minFreq = 48000.0f / (float)processor.fftSize;
        float maxFreq = 20000.0f;

        for (int i = 1; i < numBins; ++i)
        {
            float freq = (i * 48000.0f) / (float)processor.fftSize;
            if (freq > maxFreq) break;
            if (freq < minFreq) continue;

            float logX = (std::log10(freq) - std::log10(minFreq)) / (std::log10(maxFreq) - std::log10(minFreq));
            float xPos = fftBounds.getX() + (logX * fftBounds.getWidth());

            float level = juce::Decibels::gainToDecibels(processor.fftData[i]) - juce::Decibels::gainToDecibels((float)processor.fftSize);
            float yPos = juce::jmap(level, -80.0f, 0.0f, fftBounds.getBottom(), fftBounds.getY());
            yPos = juce::jlimit(fftBounds.getY(), fftBounds.getBottom(), yPos);

            if (firstFft) {
                newFftPath.startNewSubPath(xPos, yPos);
                firstFft = false;
            }
            else {
                newFftPath.lineTo(xPos, yPos);
            }
        }
        fftPath = newFftPath;
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(5);

        // Hardware Chassis 
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(area.translated(4, 5).toFloat(), 10.0f);
        juce::ColourGradient pedalGrad(juce::Colour(60, 65, 70), 0, 0, juce::Colour(20, 22, 25), 0, (float)area.getHeight(), false);
        g.setGradientFill(pedalGrad);
        g.fillRoundedRectangle(area.toFloat(), 10.0f);
        g.setColour(juce::Colours::grey.withAlpha(0.3f));
        g.drawRoundedRectangle(area.toFloat(), 10.0f, 1.5f);

        // Draw the Glass Screen
        juce::Rectangle<float> screenBounds(20.0f, 20.0f, (float)area.getWidth() - 40.0f, (float)area.getHeight() - 110.0f);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(screenBounds, 5.0f);
        g.setColour(juce::Colours::cyan.withAlpha(0.3f));
        g.drawRoundedRectangle(screenBounds, 5.0f, 2.0f);

        // Draw UI Grid Lines inside the screen
        float splitY = screenBounds.getY() + screenBounds.getHeight() * 0.5f;
        g.setColour(juce::Colours::cyan.withAlpha(0.4f));
        g.drawLine(screenBounds.getX(), splitY, screenBounds.getRight(), splitY, 1.0f); // Divider

        g.setColour(juce::Colours::limegreen.withAlpha(0.8f));
        float centerWaveY = splitY + (screenBounds.getBottom() - splitY) * 0.5f;
        g.drawLine(screenBounds.getX(), centerWaveY, screenBounds.getRight(), centerWaveY, 1.0f); // Zero-crossing

        // --- DRAW THE LIVE GRAPHS ---
        g.setColour(juce::Colours::cyan);
        g.strokePath(fftPath, juce::PathStrokeType(2.0f));

        g.setColour(juce::Colours::limegreen); // Contrast color for the time-domain wave
        g.strokePath(wavePath, juce::PathStrokeType(1.5f));

        // Draw Parameter Labels
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        int itemWidth = getWidth() / 3;
        juce::StringArray labels = { "Low", "Mid", "High" };
        for (int i = 0; i < 3; ++i) {
            g.drawText(labels[i], i * itemWidth, getHeight() - 85, itemWidth, 20, juce::Justification::centred);
        }
    }

    void resized() override
    {
        int itemWidth = getWidth() / 3;
        int knobSize = juce::jmin(itemWidth - 20, 60);
        int yOffset = getHeight() - 65;

        for (int i = 0; i < 3; ++i) {
            sliders[i]->setBounds(i * itemWidth + (itemWidth - knobSize) / 2, yOffset, knobSize, knobSize);
        }
    }

private:
    MyAmpSimAudioProcessor& processor;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::Path fftPath;
    juce::Path wavePath; 
};