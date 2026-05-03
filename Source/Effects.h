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

// --- PEDAL 7: FLANGER ---
class FlangerPedal : public AudioEffect
{
public:
    FlangerPedal(std::atomic<float>* rateParam, std::atomic<float>* depthParam, std::atomic<float>* feedParam)
        : rate(rateParam), depth(depthParam), feedback(feedParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = 2;
        flanger.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        flanger.setRate(rate->load());
        flanger.setDepth(depth->load());
        flanger.setFeedback(feedback->load());

        // Flanger magic: Center delay must be very short (e.g., 2 ms), and Mix at 50%
        flanger.setCentreDelay(2.0f);
        flanger.setMix(0.5f);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        flanger.process(context);
    }

    juce::String getName() const override { return "Flanger"; }

private:
    juce::dsp::Chorus<float> flanger; // JUCEs Chorus acts as a Flanger with short delay
    std::atomic<float>* rate, * depth, * feedback;
};

// --- PEDAL 8: PHASER ---
class PhaserPedal : public AudioEffect
{
public:
    PhaserPedal(std::atomic<float>* rateParam, std::atomic<float>* depthParam, std::atomic<float>* freqParam, std::atomic<float>* feedParam)
        : rate(rateParam), depth(depthParam), centreFreq(freqParam), feedback(feedParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = 2;
        phaser.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        phaser.setRate(rate->load());
        phaser.setDepth(depth->load());
        phaser.setCentreFrequency(centreFreq->load());
        phaser.setFeedback(feedback->load());
        phaser.setMix(0.5f);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        phaser.process(context);
    }

    juce::String getName() const override { return "Phaser"; }

private:
    juce::dsp::Phaser<float> phaser;
    std::atomic<float>* rate, * depth, * centreFreq, * feedback;
};

// --- PEDAL 9: AUTO-WAH ---
class AutoWahPedal : public AudioEffect
{
public:
    AutoWahPedal(std::atomic<float>* rateParam, std::atomic<float>* depthParam, std::atomic<float>* qParam)
        : rate(rateParam), depth(depthParam), q(qParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        lfo.reset();

        // Setup a Bandpass Filter for the Wah sound
        filterLeft.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        filterRight.setType(juce::dsp::StateVariableTPTFilterType::bandpass);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
        spec.numChannels = 1;
        filterLeft.prepare(spec);
        filterRight.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        float currentRate = rate->load();
        float currentDepth = depth->load();

        // Q (Resonance) is what gives a Wah pedal its "quack"
        filterLeft.setResonance(q->load());
        filterRight.setResonance(q->load());

        auto* leftData = buffer.getWritePointer(0);
        auto* rightData = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // LFO outputs -1.0 to 1.0  (map this to a frequency sweep)
            // Base frequency = 500Hz. Sweep goes up to 2500Hz depending on Depth.
            float rawLfo = lfo.process(currentSampleRate, currentRate);
            float sweepFreq = 500.0f + ((rawLfo + 1.0f) * 0.5f * 2000.0f * currentDepth);

            filterLeft.setCutoffFrequency(sweepFreq);
            leftData[i] = filterLeft.processSample(0, leftData[i]);

            if (rightData)
            {
                filterRight.setCutoffFrequency(sweepFreq);
                rightData[i] = filterRight.processSample(0, rightData[i]);
            }
        }
    }

    juce::String getName() const override { return "AutoWah"; }

private:
    SimpleOscillator lfo;
    juce::dsp::StateVariableTPTFilter<float> filterLeft, filterRight;
    std::atomic<float>* rate, * depth, * q;
    double currentSampleRate = 48000.0;
};

// --- PEDAL 10: COMPRESSOR ---
class CompressorPedal : public AudioEffect
{
public:
    CompressorPedal(std::atomic<float>* thresh, std::atomic<float>* ratio, std::atomic<float>* att, std::atomic<float>* rel)
        : threshold(thresh), ratio(ratio), attack(att), release(rel) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };
        comp.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        comp.setThreshold(threshold->load());
        comp.setRatio(ratio->load());
        comp.setAttack(attack->load());
        comp.setRelease(release->load());

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        comp.process(context);
    }
    juce::String getName() const override { return "Compressor"; }

private:
    juce::dsp::Compressor<float> comp;
    std::atomic<float>* threshold, * ratio, * attack, * release;
};

// --- PEDAL 11: BOOSTER ---
class BoosterPedal : public AudioEffect
{
public:
    BoosterPedal(std::atomic<float>* gainParam) : gainDb(gainParam) {}

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };
        gain.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        gain.setGainDecibels(gainDb->load());

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        gain.process(context);
    }
    juce::String getName() const override { return "Booster"; }

private:
    juce::dsp::Gain<float> gain;
    std::atomic<float>* gainDb;
};

// --- PEDAL 12: 3-BAND EQ ---
class EqPedal : public AudioEffect
{
public:
    EqPedal(std::atomic<float>* low, std::atomic<float>* mid, std::atomic<float>* high)
        : lowDb(low), midDb(mid), highDb(high) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };
        lowFilter.prepare(spec);
        midFilter.prepare(spec);
        highFilter.prepare(spec);

        updateFilters(); // Set initial state
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        // Only recalculate filters if a knob actually moved
        if (lastLow != lowDb->load() || lastMid != midDb->load() || lastHigh != highDb->load())
            updateFilters();

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        lowFilter.process(context);
        midFilter.process(context);
        highFilter.process(context);
    }
    juce::String getName() const override { return "EQ"; }

private:
    void updateFilters()
    {
        lastLow = lowDb->load(); lastMid = midDb->load(); lastHigh = highDb->load();

        *lowFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(currentSampleRate, 250.0f, 0.707f, juce::Decibels::decibelsToGain(lastLow));
        *midFilter.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, 1000.0f, 0.707f, juce::Decibels::decibelsToGain(lastMid));
        *highFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(currentSampleRate, 4000.0f, 0.707f, juce::Decibels::decibelsToGain(lastHigh));
    }

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> lowFilter, midFilter, highFilter;
    std::atomic<float>* lowDb, * midDb, * highDb;
    float lastLow = -100.0f, lastMid = -100.0f, lastHigh = -100.0f; // Track changes
    double currentSampleRate = 48000.0;
};