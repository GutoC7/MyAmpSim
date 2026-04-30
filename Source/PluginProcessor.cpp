#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "AmpMath.h"

// --- THE PROCESSOR (Audio Engine) ---
class MyAmpSimAudioProcessor : public juce::AudioProcessor
{
public:
    // Parameters
    std::atomic<float> driveParameter{ 50.0f };
    std::atomic<float> outputLevel{ 1.0f };
    std::atomic<float> tremDepth{ 0.0f };
    std::atomic<float> tremRate{ 5.0f };
    std::atomic<float> delayTime{ 0.5f };
    std::atomic<float> delayFeedback{ 0.3f };
    std::atomic<float> delayMix{ 0.0f };

    // Objects
    juce::dsp::Convolution cabSim;
    SimpleOscillator lfo;
    DelayLine delayLeft;
    DelayLine delayRight;
    ParameterSmoother smoothDelayTime;

    MyAmpSimAudioProcessor() : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    }
    ~MyAmpSimAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        // Setup Cab Sim
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = getTotalNumOutputChannels();
        cabSim.prepare(spec);

        juce::File file("C:/Documents/GitHub/MyAmpSim/cabinet.wav");

        if (file.existsAsFile())
        {
            cabSim.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0);
        }
        // Setup Time Effects
        lfo.reset();
        delayLeft.prepare(sampleRate);
        delayRight.prepare(sampleRate);
        smoothDelayTime.reset(sampleRate, 0.05); // 50ms slide time
        smoothDelayTime.setCurrentAndTarget(delayTime.load());
    }

    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        juce::ScopedNoDenormals noDenormals;

        float currentDrive = driveParameter.load();
        float currentVol = outputLevel.load();
        float currentDepth = tremDepth.load();
        float currentRate = tremRate.load();
        float dFeed = delayFeedback.load();
        float dMix = delayMix.load();
        float currentSampleRate = getSampleRate();

        smoothDelayTime.setTargetValue(delayTime.load());

        // Clear garbage
        for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
            buffer.clear(i, 0, buffer.getNumSamples());

        // STAGE 1: Distortion
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = TubeDistortion::process(data[i] * currentDrive, 1.0f); // Normalized drive inside tanh
        }

        // STAGE 2: Cab Sim (Convolution Block)
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        cabSim.process(context);

        // STAGE 3: Time Effects (Tremolo -> Delay)
        auto* leftData = buffer.getWritePointer(0);
        auto* rightData = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float smoothTime = smoothDelayTime.getNextValue();
            float rawLfo = lfo.process(currentSampleRate, currentRate);
            float tremGain = 1.0f - (currentDepth * 0.5f * (1.0f + rawLfo));

            // Left
            float inL = leftData[i] * tremGain;
            float echoL = delayLeft.process(inL, dFeed, smoothTime, currentSampleRate);
            leftData[i] = (inL + echoL * dMix) * currentVol;

            // Right
            if (rightData)
            {
                float inR = rightData[i] * tremGain;
                float echoR = delayRight.process(inR, dFeed, smoothTime, currentSampleRate);
                rightData[i] = (inR + echoR * dMix) * currentVol;
            }
        }
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
};

// --- THE EDITOR (GUI) ---
class MyAmpSimEditor : public juce::AudioProcessorEditor
{
public:
    MyAmpSimEditor(MyAmpSimAudioProcessor& p) : AudioProcessorEditor(&p), audioProcessor(p)
    {
        setSize(850, 300); // Perfect size for 6 knobs

        setupKnob(driveKnob, 1.0f, 100.0f, 50.0f, [this] { audioProcessor.driveParameter.store(driveKnob.getValue()); });
        setupKnob(tremKnob, 0.0f, 1.0f, 0.0f, [this] { audioProcessor.tremDepth.store(tremKnob.getValue()); });
        setupKnob(dTimeKnob, 0.01f, 1.0f, 0.5f, [this] { audioProcessor.delayTime.store(dTimeKnob.getValue()); });
        setupKnob(dFeedKnob, 0.0f, 0.9f, 0.3f, [this] { audioProcessor.delayFeedback.store(dFeedKnob.getValue()); });
        setupKnob(dMixKnob, 0.0f, 1.0f, 0.0f, [this] { audioProcessor.delayMix.store(dMixKnob.getValue()); });
        setupKnob(volKnob, 0.0f, 2.0f, 1.0f, [this] { audioProcessor.outputLevel.store(volKnob.getValue()); });
    }

    void setupKnob(juce::Slider& slider, float min, float max, float defaultVal, std::function<void()> callback)
    {
        slider.setSliderStyle(juce::Slider::Rotary);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
        slider.setRange(min, max, 0.01f);
        slider.setValue(defaultVal);
        slider.onValueChange = callback;
        addAndMakeVisible(slider);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::darkgrey);
        g.setColour(juce::Colours::white);
        g.setFont(20.0f);
        g.drawFittedText("My Pro Amp Sim", getLocalBounds().removeFromTop(30), juce::Justification::centred, 1);

        g.drawText("Drive", 40, 50, 100, 100, juce::Justification::centred);
        g.drawText("Tremolo", 170, 50, 100, 100, juce::Justification::centred);
        g.drawText("Echo Time", 300, 50, 100, 100, juce::Justification::centred);
        g.drawText("Echo Feed", 430, 50, 100, 100, juce::Justification::centred);
        g.drawText("Echo Mix", 560, 50, 100, 100, juce::Justification::centred);
        g.drawText("Master Vol", 690, 50, 100, 100, juce::Justification::centred);
    }

    void resized() override
    {
        driveKnob.setBounds(40, 50, 100, 100);
        tremKnob.setBounds(170, 50, 100, 100);
        dTimeKnob.setBounds(300, 50, 100, 100);
        dFeedKnob.setBounds(430, 50, 100, 100);
        dMixKnob.setBounds(560, 50, 100, 100);
        volKnob.setBounds(690, 50, 100, 100);
    }

private:
    MyAmpSimAudioProcessor& audioProcessor;
    juce::Slider driveKnob, tremKnob, dTimeKnob, dFeedKnob, dMixKnob, volKnob;
};

juce::AudioProcessorEditor* MyAmpSimAudioProcessor::createEditor() { return new MyAmpSimEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyAmpSimAudioProcessor(); }