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

    std::atomic<float>* isBypassed = nullptr;
};

// --- PEDAL 1: MULTI-DISTORTION (Upgraded with 4x Oversampling) ---
class DistortionPedal : public AudioEffect
{
public:
    DistortionPedal(std::atomic<float>* driveParam, std::atomic<float>* typeParam)
        : drive(driveParam), distType(typeParam),
        // 2 channels, factor 2 = 4x oversampling, high quality filter, use integer latency
        oversampler(2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true)
    {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        // The oversampler needs to know the block size to allocate memory safely
        oversampler.initProcessing(static_cast<size_t>(samplesPerBlock));
        oversampler.reset();
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        // If bypassed do absolutely nothing
        if (isBypassed && isBypassed->load() > 0.5f) return;

        float currentDrive = drive->load();
        int currentType = static_cast<int>(distType->load()); // 0=Tube, 1=OD, 2=Fuzz

        // 1. UPSAMPLE: Convert the buffer to an AudioBlock, then upsample to 4x (e.g., 192kHz)
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::AudioBlock<float> upsampledBlock = oversampler.processSamplesUp(block);

        // 2. PROCESS AUDIO AT ULTRA-HIGH RESOLUTION
        for (int channel = 0; channel < (int)upsampledBlock.getNumChannels(); ++channel)
        {
            auto* data = upsampledBlock.getChannelPointer(channel);
            for (int i = 0; i < (int)upsampledBlock.getNumSamples(); ++i)
            {
                // Uses the exact same AmpMath logic, just running at 4x the speed
                data[i] = MultiDistortion::process(data[i], currentDrive, currentType);
            }
        }

        // 3. DOWNSAMPLE: Filter out the high-frequency garbage and return to original sample rate
        oversampler.processSamplesDown(block);
    }

    juce::String getName() const override { return "Distortion"; }

private:
    std::atomic<float>* drive;
    std::atomic<float>* distType;
    juce::dsp::Oversampling<float> oversampler;
};

// --- PEDAL 2: CABINET SIMULATOR ---
// (No APVTS parameters since it has no knobs)
class CabinetPedal : public AudioEffect
{
public:
    CabinetPedal() = default;

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };
        convolution.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        // Convolution requires wrapping the buffer in an AudioBlock
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        convolution.process(context);
    }

    // Custom function to load the .wav file from the UI
    void loadImpulseResponse(const juce::File& file)
    {
        // Trims silence and normalizes the IR automatically
        convolution.loadImpulseResponse(file, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0);
    }
    
    // Load IR directly from compiled RAM
    void loadImpulseResponseFromMemory(const void* sourceData, size_t sourceDataSize)
    {
        convolution.loadImpulseResponse(sourceData, sourceDataSize,
            juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0);
    }

    juce::String getName() const override { return "Cab"; }

private:
    juce::dsp::Convolution convolution;
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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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

// --- PEDAL 6: CHORUS (Analog BBD Emulation) ---
class ChorusPedal : public AudioEffect
{
public:
    ChorusPedal(std::atomic<float>* rateParam, std::atomic<float>* depthParam, std::atomic<float>* mixParam)
        : rate(rateParam), depth(depthParam), mix(mixParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };

        chorus.prepare(spec);
        warmthFilter.prepare(spec);

        // Analog Emulation: Roll off the harsh digital highs on the wet chorus signal
        *warmthFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 3000.0f, 0.707f);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        chorus.setRate(rate->load());
        chorus.setDepth(depth->load());
        chorus.setMix(mix->load());
        chorus.setCentreDelay(15.0f); // 15ms is the classic Boss CE-2 sweet spot
        chorus.setFeedback(0.0f);

        // 1. Create a copy of the dry signal
        juce::AudioBuffer<float> dryBuffer;
        dryBuffer.makeCopyOf(buffer);

        // 2. Process the main buffer through the Chorus
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        chorus.process(context);

        // 3. Process the chorused audio through the Analog Warmth Filter
        warmthFilter.process(context);

        // 4. Mix the warm, dark chorus back in with the pristine dry signal
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            auto* outData = buffer.getWritePointer(channel);
            const auto* dryData = dryBuffer.getReadPointer(channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                // The "Mix" knob balances the pristine dry signal with the warm chorused signal
                outData[i] = (dryData[i] * 0.5f) + (outData[i] * 0.5f);
            }
        }
    }

    juce::String getName() const override { return "Chorus"; }

private:
    juce::dsp::Chorus<float> chorus;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> warmthFilter;
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
        if (isBypassed && isBypassed->load() > 0.5f) return;
        
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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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
        if (isBypassed && isBypassed->load() > 0.5f) return;
        
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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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
        if (isBypassed && isBypassed->load() > 0.5f) return;

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

// --- PEDAL 13: NOISE GATE ---
class NoiseGatePedal : public AudioEffect
{
public:
    NoiseGatePedal(std::atomic<float>* thresh, std::atomic<float>* ratio, std::atomic<float>* att, std::atomic<float>* rel)
        : threshold(thresh), ratio(ratio), attack(att), release(rel) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };
        gate.prepare(spec);
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        gate.setThreshold(threshold->load());
        gate.setRatio(ratio->load());
        gate.setAttack(attack->load());
        gate.setRelease(release->load());

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        gate.process(context);
    }
    juce::String getName() const override { return "NoiseGate"; }

private:
    juce::dsp::NoiseGate<float> gate;
    std::atomic<float>* threshold, * ratio, * attack, * release;
};

// --- PEDAL 14: PITCH SHIFTER (Granular Tape Heads) ---
class PitchShifterPedal : public AudioEffect
{
public:
    PitchShifterPedal(std::atomic<float>* shiftSemi, std::atomic<float>* mix)
        : semitones(shiftSemi), wetMix(mix) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        // A 100ms buffer is plenty of room for pitch shifting grains
        int bufferSize = static_cast<int>(sampleRate * 0.1f);
        delayBuffer.setSize(2, bufferSize);
        delayBuffer.clear();
        writePos = 0;
        phaseA = 0.0f;
        phaseB = 0.5f; // Head B is exactly 180 degrees out of phase with Head A
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        float shift = semitones->load();
        float mixVal = wetMix->load();

        // If shifting by 0, just act as True Bypass to save CPU
        if (std::abs(shift) < 0.01f) return;

        // Ratio: 1.0 = normal, 2.0 = octave up, 0.5 = octave down
        float ratio = std::pow(2.0f, shift / 12.0f);

        // The grain size determines the latency and low-freq response. ~40ms is standard for guitar.
        float grainSizeSamples = currentSampleRate * 0.04f;

        // How fast the read heads move relative to the write head
        float phaseIncrement = (1.0f - ratio) / grainSizeSamples;

        // Process per channel to preserve L1 Cache speed
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            auto* delayData = delayBuffer.getWritePointer(channel);
            int bufferLength = delayBuffer.getNumSamples();

            // Use local variables to guarantee Left and Right channels stay perfectly synced!
            float localPhaseA = phaseA;
            float localPhaseB = phaseB;
            int localWritePos = writePos;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float inSample = channelData[i];
                delayData[localWritePos] = inSample;

                // 1. Calculate delay times for the two read heads
                float delayA = localPhaseA * grainSizeSamples;
                float delayB = localPhaseB * grainSizeSamples;

                // 2. Wrap the read positions around the circular buffer safely
                float readPosA = localWritePos - delayA;
                if (readPosA < 0.0f) readPosA += bufferLength;

                float readPosB = localWritePos - delayB;
                if (readPosB < 0.0f) readPosB += bufferLength;

                // 3. Linear Interpolation for Head A (Removes Zipper Noise)
                int indexA1 = static_cast<int>(readPosA);
                int indexA2 = indexA1 + 1;
                if (indexA2 >= bufferLength) indexA2 -= bufferLength;
                float fracA = readPosA - indexA1;
                float sampleA = delayData[indexA1] * (1.0f - fracA) + delayData[indexA2] * fracA;

                // 4. Linear Interpolation for Head B
                int indexB1 = static_cast<int>(readPosB);
                int indexB2 = indexB1 + 1;
                if (indexB2 >= bufferLength) indexB2 -= bufferLength;
                float fracB = readPosB - indexB1;
                float sampleB = delayData[indexB1] * (1.0f - fracB) + delayData[indexB2] * fracB;

                // 5. Apply Triangle Windows (Fade grains out before they reset)
                float windowA = 1.0f - std::abs(localPhaseA * 2.0f - 1.0f);
                float windowB = 1.0f - std::abs(localPhaseB * 2.0f - 1.0f);

                // Mix the heads and apply the wet/dry knob
                float shiftedSample = (sampleA * windowA) + (sampleB * windowB);
                channelData[i] = (inSample * (1.0f - mixVal)) + (shiftedSample * mixVal);

                // 6. Advance LFO phases
                localPhaseA += phaseIncrement;
                if (localPhaseA >= 1.0f) localPhaseA -= 1.0f;
                else if (localPhaseA < 0.0f) localPhaseA += 1.0f;

                localPhaseB += phaseIncrement;
                if (localPhaseB >= 1.0f) localPhaseB -= 1.0f;
                else if (localPhaseB < 0.0f) localPhaseB += 1.0f;

                // 7. Advance Write Pointer
                localWritePos++;
                if (localWritePos >= bufferLength) localWritePos = 0;
            }

            // Once the final channel (Right) is finished, permanently update the class variables
            if (channel == buffer.getNumChannels() - 1)
            {
                phaseA = localPhaseA;
                phaseB = localPhaseB;
                writePos = localWritePos;
            }
        }
    }
    juce::String getName() const override { return "PitchShifter"; }

private:
    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    float phaseA = 0.0f, phaseB = 0.5f;
    double currentSampleRate = 48000.0;
    std::atomic<float>* semitones, * wetMix;
};

// --- PEDAL 15: OCTAVER ---
// An Octaver is mathematically identical to a Pitch Shifter, but locked to -12, +12, or -24 semitones
// Due to lazyness (SWE principles) just inherit from the Pitch Shifter and lock the parameters
class OctaverPedal : public PitchShifterPedal
{
public:
    OctaverPedal(std::atomic<float>* shiftSemi, std::atomic<float>* mix)
        : PitchShifterPedal(shiftSemi, mix) {
    }

    juce::String getName() const override { return "Octaver"; }
};

// --- PEDAL 16: ACOUSTIC SIMULATOR ---
class AcousticSimPedal : public AudioEffect
{
public:
    AcousticSimPedal(std::atomic<float>* bodyParam, std::atomic<float>* airParam, std::atomic<float>* resoParam)
        : bodyDb(bodyParam), airDb(airParam), resoMix(resoParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec{ sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2 };

        bodyFilter.prepare(spec);
        midScoop.prepare(spec);
        airFilter.prepare(spec);
        bodyResonance.prepare(sampleRate);

        // Scoop the magnetic "boxy" mids permanently
        *midScoop.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, 600.0f, 0.707f, juce::Decibels::decibelsToGain(-8.0f));
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        // Dynamic EQs
        *bodyFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(currentSampleRate, 150.0f, 0.707f, juce::Decibels::decibelsToGain(bodyDb->load()));
        *airFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(currentSampleRate, 4000.0f, 0.707f, juce::Decibels::decibelsToGain(airDb->load()));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        bodyFilter.process(context);
        midScoop.process(context);
        airFilter.process(context);

        // Add hollow wooden body resonance (a 3ms micro-delay)
        float mix = resoMix->load();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            auto* data = buffer.getWritePointer(channel);
            for (int i = 0; i < buffer.getNumSamples(); ++i) {
                float echo = bodyResonance.process(data[i], 0.4f, 0.003f, currentSampleRate);
                data[i] = data[i] + (echo * mix);
            }
        }
    }
    juce::String getName() const override { return "AcousticSim"; }

private:
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> bodyFilter, midScoop, airFilter;
    DelayLine bodyResonance;
    std::atomic<float>* bodyDb, * airDb, * resoMix;
    double currentSampleRate = 48000.0;
};

// --- PEDAL 17: GUITAR SYNTH (Upgraded for MIDI) ---
class GuitarSynthPedal : public AudioEffect
{
public:
    // UPGRADE: Added midiPitch parameter to constructor
    GuitarSynthPedal(std::atomic<float>* typeParam, std::atomic<float>* mixParam, std::atomic<float>* trackedPitch, std::atomic<float>* midiPitch)
        : waveType(typeParam), mix(mixParam), pitchHz(trackedPitch), currentMidiHz(midiPitch) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override { currentSampleRate = sampleRate; }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        // If a MIDI key is pressed override the tuner's tracking
        float midiHz = currentMidiHz->load();
        float hz = (midiHz > 10.0f) ? midiHz : pitchHz->load();

        float currentMix = mix->load();
        int type = static_cast<int>(waveType->load());

        auto* leftData = buffer.getWritePointer(0);
        auto* rightData = (buffer.getNumChannels() > 1) ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float inSample = leftData[i];
            float env = envFollower.process(inSample, 5.0f, 50.0f, currentSampleRate);

			// Only generate synth tone if we have a valid frequency and the envelope is open enough
            float synthTone = 0.0f;
            if (hz > 40.0f && env > 0.01f) {
                synthTone = osc.process(currentSampleRate, hz, type) * env * 2.0f;
            }

			// Mix the synth tone with the original guitar signal based on the Mix knob
            leftData[i] = (inSample * (1.0f - currentMix)) + (synthTone * currentMix);
            if (rightData) rightData[i] = leftData[i];
        }
    }
    juce::String getName() const override { return "GuitarSynth"; }

private:
    SimpleOscillator osc;
    EnvelopeFollower envFollower;
    std::atomic<float>* waveType, * mix, * pitchHz, * currentMidiHz;
    double currentSampleRate = 48000.0;
};

// --- PEDAL 18: THE AUDIO LOOPER ---
class LooperPedal : public AudioEffect
{
public:
    LooperPedal(std::atomic<float>* stateParam, std::atomic<float>* levelParam)
        : state(stateParam), level(levelParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override
    {
        // Allocate exactly 60 seconds of stereo RAM (approx 23 MB)
		// Memory allocation must happen here, never in the real-time audio thread bcause it can cause dropouts
        int maxSamples = static_cast<int>(sampleRate * 60.0);
        looperBuffer.setSize(2, maxSamples);
        looperBuffer.clear();

        writePos = 0;
        readPos = 0;
        loopLength = 0;
        lastState = 0;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        int currentState = static_cast<int>(state->load()); // 0:Stop, 1:Rec, 2:Play, 3:Dub
        float currentLevel = level->load();

        // STATE MACHINE LOGIC
        if (currentState != lastState)
        {
            if (currentState == 1) // Transition to RECORD
            {
                writePos = 0;
                loopLength = 0;
                looperBuffer.clear(); // Erase old loop
            }
            else if (lastState == 1 && (currentState == 2 || currentState == 3))
            {
                // Transitioning out of Record: Lock in the total loop length
                loopLength = writePos;
                readPos = 0;
            }
            else if (currentState == 0) // Transition to stop
            {
                readPos = 0;
                writePos = 0;
            }
            lastState = currentState;
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            auto* loopData = looperBuffer.getWritePointer(channel);

            // Local variables ensure L/R stereo channels stay perfectly synced
            int localWrite = writePos;
            int localRead = readPos;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float inSample = channelData[i];
                float outSample = inSample; // Always pass dry signal through

                if (currentState == 1) // RECORDING
                {
                    if (localWrite < looperBuffer.getNumSamples()) {
                        loopData[localWrite] = inSample;
                        localWrite++;
                    }
                }
                else if (currentState == 2 && loopLength > 0) // PLAYING
                {
                    float recordedSample = loopData[localRead];
                    outSample = inSample + (recordedSample * currentLevel);

                    localRead++;
                    if (localRead >= loopLength) localRead = 0;
                }
                else if (currentState == 3 && loopLength > 0) // OVERDUBBING
                {
                    // 1. Read existing tape
                    float recordedSample = loopData[localRead];
                    // 2. Write new mix back to tape
                    loopData[localRead] = recordedSample + inSample;
                    // 3. Output the combined mix
                    outSample = inSample + (loopData[localRead] * currentLevel);

                    localRead++;
                    if (localRead >= loopLength) localRead = 0;
                }

                channelData[i] = outSample;
            }

            // Push local pointers back to class state once per block
            if (channel == buffer.getNumChannels() - 1) {
                writePos = localWrite;
                readPos = localRead;
            }
        }
    }
    juce::String getName() const override { return "Looper"; }

private:
    juce::AudioBuffer<float> looperBuffer;
    int writePos = 0, readPos = 0, loopLength = 0, lastState = 0;
    std::atomic<float>* state, * level;
};

// --- PEDAL 19: BITCRUSHER (Retro Hardware Sim) ---
class BitcrusherPedal : public AudioEffect
{
public:
    BitcrusherPedal(std::atomic<float>* bitsParam, std::atomic<float>* rateParam)
        : bitDepth(bitsParam), downsampleFactor(rateParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override {}

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        float bits = bitDepth->load();
        int downsample = static_cast<int>(downsampleFactor->load());

        // Calculate the number of discrete steps available at this bit-depth
        float steps = std::pow(2.0f, bits);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);

            // Keep local tracking per channel for the sample-hold effect
            int localCounter = sampleCounter[channel];
            float localHeld = heldSample[channel];

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (localCounter >= downsample) {
                    // 1. Quantize the amplitude (Bit Reduction)
                    float sample = data[i];
                    localHeld = std::round(sample * steps) / steps;
                    localCounter = 0;
                }
                else {
                    localCounter++;
                }

                // 2. Output the held sample (Sample Rate Reduction / Aliasing)
                data[i] = localHeld;
            }

            if (channel == buffer.getNumChannels() - 1) {
                sampleCounter[channel] = localCounter;
                heldSample[channel] = localHeld;
            }
        }
    }
    juce::String getName() const override { return "Bitcrush"; }

private:
    std::atomic<float>* bitDepth, * downsampleFactor;
    int sampleCounter[2] = { 0, 0 };
    float heldSample[2] = { 0.0f, 0.0f };
};

// --- PEDAL 20: RING MODULATOR ---
class RingModPedal : public AudioEffect
{
public:
    RingModPedal(std::atomic<float>* freqParam, std::atomic<float>* mixParam)
        : freq(freqParam), mix(mixParam) {
    }

    void prepare(double sampleRate, int samplesPerBlock) override {
        currentSampleRate = sampleRate;
        phase = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) override
    {
        if (isBypassed && isBypassed->load() > 0.5f) return;

        float currentFreq = freq->load();
        float currentMix = mix->load();
        float phaseIncrement = juce::MathConstants<float>::twoPi * currentFreq / currentSampleRate;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            float localPhase = phase;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float inSample = data[i];
                float mod = std::sin(localPhase);
                float ringSample = inSample * mod;

                data[i] = (inSample * (1.0f - currentMix)) + (ringSample * currentMix);

                localPhase += phaseIncrement;
                if (localPhase >= juce::MathConstants<float>::twoPi)
                    localPhase -= juce::MathConstants<float>::twoPi;
            }

            if (channel == buffer.getNumChannels() - 1) phase = localPhase;
        }
    }
    juce::String getName() const override { return "RingMod"; }

private:
    std::atomic<float>* freq, * mix;
    float phase = 0.0f;
    double currentSampleRate = 48000.0;
};