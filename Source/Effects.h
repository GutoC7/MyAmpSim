#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "BinaryData.h"
#include "AmpMath.h"

// --- THE INTERFACE ---
class AudioEffect
{
public:
    virtual ~AudioEffect() = default;
    virtual void prepare(double sampleRate, int samplesPerBlock) = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) = 0;
    virtual juce::String getName() const = 0;
};

// --- PEDAL 1: DISTORTION ---
class DistortionPedal : public AudioEffect
{
public:
    // APVTS returns pointers, so store a pointer
    DistortionPedal(std::atomic<float>* driveParam) : drive(driveParam) {}

    void prepare(double sampleRate, int samplesPerBlock) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        // Use -> instead of . for pointers
        float currentDrive = drive->load();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                data[i] = TubeDistortion::process(data[i] * currentDrive, 1.0f);
            }
        }
    }
    juce::String getName() const override { return "Distortion"; }

private:
    std::atomic<float>* drive;
};

// --- PEDAL 2: CABINET SIMULATOR ---
// (No APVTS parameters since it has no knobs)
class CabinetPedal : public AudioEffect
{
public:
    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = 2;
        cabSim.prepare(spec);

        const void* cabData = BinaryData::cabinet_wav;
        int cabSize = BinaryData::cabinet_wavSize;
        if (cabData != nullptr && cabSize > 0)
            cabSim.loadImpulseResponse(cabData, cabSize, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        cabSim.process(context);
    }
    juce::String getName() const override { return "Cabinet"; }

private:
    juce::dsp::Convolution cabSim;
};

// --- PEDAL 3: TREMOLO ---
class TremoloPedal : public AudioEffect
{
public:
    TremoloPedal(std::atomic<float>* depthParam, std::atomic<float>* rateParam)
        : depth(depthParam), rate(rateParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        lfo.reset();
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        float currentDepth = depth->load();
        float currentRate = rate->load();
        auto* leftData = buffer.getWritePointer(0);
        auto* rightData = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float rawLfo = lfo.process(currentSampleRate, currentRate);
            float tremGain = 1.0f - (currentDepth * 0.5f * (1.0f + rawLfo));

            leftData[i] *= tremGain;
            if (rightData) rightData[i] *= tremGain;
        }
    }
    juce::String getName() const override { return "Tremolo"; }

private:
    SimpleOscillator lfo;
    std::atomic<float>* depth;
    std::atomic<float>* rate;
    double currentSampleRate = 48000.0;
};

// --- PEDAL 4: DELAY ---
class DelayPedal : public AudioEffect
{
public:
    DelayPedal(std::atomic<float>* timeParam, std::atomic<float>* feedParam, std::atomic<float>* mixParam)
        : time(timeParam), feed(feedParam), mix(mixParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        delayLeft.prepare(sampleRate);
        delayRight.prepare(sampleRate);
        smoothDelayTime.reset(sampleRate, 0.05);

        // Initialize smoother with the pointer's value
        smoothDelayTime.setCurrentAndTarget(time->load());
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        smoothDelayTime.setTargetValue(time->load());
        float dFeed = feed->load();
        float dMix = mix->load();

        auto* leftData = buffer.getWritePointer(0);
        auto* rightData = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float smoothTime = smoothDelayTime.getNextValue();

            float inL = leftData[i];
            float echoL = delayLeft.process(inL, dFeed, smoothTime, currentSampleRate);
            leftData[i] = inL + (echoL * dMix);

            if (rightData)
            {
                float inR = rightData[i];
                float echoR = delayRight.process(inR, dFeed, smoothTime, currentSampleRate);
                rightData[i] = inR + (echoR * dMix);
            }
        }
    }
    juce::String getName() const override { return "Delay"; }

private:
    DelayLine delayLeft;
    DelayLine delayRight;
    ParameterSmoother smoothDelayTime;
    std::atomic<float>* time;
    std::atomic<float>* feed;
    std::atomic<float>* mix;
    double currentSampleRate = 48000.0;
};

// --- PEDAL 5: REVERB ---
class ReverbPedal : public AudioEffect
{
public:
    ReverbPedal(std::atomic<float>* roomParam, std::atomic<float>* dampParam, std::atomic<float>* mixParam)
        : roomSize(roomParam), damping(dampParam), wetMix(mixParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        reverb.setSampleRate(sampleRate);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        // Update parameters dynamically
        juce::Reverb::Parameters params;
        params.roomSize = roomSize->load();
        params.damping = damping->load();
        params.wetLevel = wetMix->load();
        params.dryLevel = 1.0f - (wetMix->load() * 0.5f); // Slight volume dip when very wet
        params.width = 1.0f; // Full stereo
        reverb.setParameters(params);

        // Process audio (JUCE Reverb handles stereo internally)
        if (buffer.getNumChannels() == 1)
            reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
        else
            reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
    }

    juce::String getName() const override { return "Reverb"; }

private:
    juce::Reverb reverb;
    std::atomic<float>* roomSize, * damping, * wetMix;
};

// --- PEDAL 6: CHORUS ---
class ChorusPedal : public AudioEffect
{
public:
    ChorusPedal(std::atomic<float>* rateParam, std::atomic<float>* depthParam, std::atomic<float>* mixParam)
        : rate(rateParam), depth(depthParam), mix(mixParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = 2;
        chorus.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        chorus.setRate(rate->load());
        chorus.setDepth(depth->load());
        chorus.setMix(mix->load());
        chorus.setCentreDelay(7.0f); // 7ms is the sweet spot for Chorus
        chorus.setFeedback(0.0f);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        chorus.process(context);
    }

    juce::String getName() const override { return "Chorus"; }

private:
    juce::dsp::Chorus<float> chorus;
    std::atomic<float>* rate, * depth, * mix;
};