#pragma once
#include <JuceHeader.h>

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

// CUSTOM CABINET UI BLOCK
class CabinetUIBlock : public juce::Component
{
public:
    CabinetUIBlock(std::function<void(const juce::File&)> onLoadFile)
        : loadCallback(onLoadFile)
    {
        addAndMakeVisible(btnLoadIR);
        btnLoadIR.setButtonText("LOAD .WAV IMPULSE");
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
    }

    void paint(juce::Graphics& g) override
    {
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
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("Cabinet Impulse Response", 20, 20, 300, 30, juce::Justification::centredLeft);

        g.setFont(juce::Font(14.0f, juce::Font::italic));
        g.setColour(juce::Colours::cyan);
        g.drawText(irName.isEmpty() ? "No IR Loaded (Bypassed)" : "Loaded: " + irName, 20, 60, 400, 30, juce::Justification::centredLeft);
    }

    void resized() override
    {
        btnLoadIR.setBounds(400, 45, 160, 40);
    }

private:
    juce::TextButton btnLoadIR;
    juce::String irName;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::function<void(const juce::File&)> loadCallback;
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