#pragma once
#include <cmath>
#include <vector>

class MultiDistortion
{
public:
    static float process(float input, float drive, int type)
    {
        float x = input * drive;

        if (type == 0) // 1. TUBE: Smooth analog clipping
            return std::tanh(x);

        if (type == 1) // 2. OVERDRIVE: Asymmetrical clipping (tube-amp style)
            return x > 0 ? std::tanh(x) : (std::exp(x) - 1.0f);

        if (type == 2) // 3. FUZZ: Brutal, square-wave hard clipping
            return x > 0.1f ? 1.0f : (x < -0.1f ? -1.0f : x * 10.0f);

        return x;
    }
};

class SimpleOscillator
{
public:
    SimpleOscillator() = default;

    // type: 0 = Sine, 1 = Square, 2 = Saw
    float process(float sampleRate, float frequency, int type = 0)
    {
        if (frequency <= 0.0f) return 0.0f;

        float phaseIncrement = 6.28318530718f * frequency / sampleRate;
        currentPhase += phaseIncrement;
        if (currentPhase > 6.28318530718f) currentPhase -= 6.28318530718f;

        if (type == 0) return std::sin(currentPhase);
        if (type == 1) return currentPhase < 3.14159265359f ? 1.0f : -1.0f;
        return (currentPhase / 3.14159265359f) - 1.0f; // Sawtooth
    }
    void reset() { currentPhase = 0.0f; }
private:
    float currentPhase = 0.0f;
};

class EnvelopeFollower
{
public:
    float process(float sample, float attackMs, float releaseMs, float sampleRate)
    {
        float absSample = std::abs(sample);
        float attackCoef = std::exp(-1.0f / (attackMs * 0.001f * sampleRate));
        float releaseCoef = std::exp(-1.0f / (releaseMs * 0.001f * sampleRate));

        if (absSample > envelope)
            envelope = attackCoef * envelope + (1.0f - attackCoef) * absSample;
        else
            envelope = releaseCoef * envelope + (1.0f - releaseCoef) * absSample;

        return envelope;
    }
private:
    float envelope = 0.0f;
};

class DelayLine
{
public:
    DelayLine() { buffer.resize(96000, 0.0f); }

    void prepare(double sampleRate)
    {
        if (buffer.size() < 2 * sampleRate) buffer.resize((int)(2 * sampleRate));
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    float process(float inputSample, float feedbackAmount, float delayTimeSeconds, float sampleRate)
    {
        float delaySamples = delayTimeSeconds * sampleRate;
        float readIndex = (float)writeIndex - delaySamples;
        if (readIndex < 0.0f) readIndex += buffer.size();

        int cleanReadIndex = (int)readIndex;
        float delayedSample = buffer[cleanReadIndex];

        buffer[writeIndex] = inputSample + (delayedSample * feedbackAmount);

        writeIndex++;
        if (writeIndex >= buffer.size()) writeIndex = 0;

        return delayedSample;
    }
private:
    std::vector<float> buffer;
    int writeIndex = 0;
};

class ParameterSmoother
{
public:
    ParameterSmoother() = default;
    void reset(double sampleRate, double smoothingTimeInSeconds)
    {
        if (smoothingTimeInSeconds <= 0.0) coeff = 1.0f;
        else coeff = 1.0f - std::exp(-1.0f / (smoothingTimeInSeconds * sampleRate));
        currentValue = targetValue;
    }
    void setTargetValue(float target) { targetValue = target; }
    float getNextValue()
    {
        currentValue += (targetValue - currentValue) * coeff;
        return currentValue;
    }
    void setCurrentAndTarget(float val) { currentValue = val; targetValue = val; }
private:
    float currentValue = 0.0f;
    float targetValue = 0.0f;
    float coeff = 1.0f;
};

class GuitarTuner
{
public:
    void prepare(double sr) { sampleRate = sr; }
    void process(float sample)
    {
        // 1. ENVELOPE FOLLOWER (Noise Gate)
        // Track the volume. If it's too quiet, ignore it completely.
        float absSample = std::abs(sample);
        envelope = envelope * 0.99f + absSample * 0.01f;

        if (envelope < 0.005f) { // Noise threshold
            return; // Exit early, don't update pitch
        }

        // 2. HEAVY LOWPASS FILTER
        // Smooth out the string harmonics to find the fundamental note
        filtered = filtered * 0.9f + sample * 0.1f;

        // 3. ZERO-CROSSING WITH HYSTERESIS
        // The wave must cross +0.01 AND -0.01 to count as a full cycle. 
        // This ignores the tiny harmonic wiggles near the zero line.
        if (filtered > 0.01f)
        {
            isPositive = true;
        }
        else if (filtered < -0.01f && isPositive)
        {
            isPositive = false;
            float period = currentSampleIndex - lastZeroCross;

            if (period > 0)
            {
                float hz = sampleRate / period;
                if (hz > 60.0f && hz < 1000.0f) // Restrict to standard guitar range
                    smoothedHz = smoothedHz * 0.9f + hz * 0.1f;
            }
            lastZeroCross = currentSampleIndex;
        }
        currentSampleIndex++;
    }

    // If the envelope is below our noise gate, output 0 Hz
    float getHz() const { return envelope < 0.005f ? 0.0f : smoothedHz; }

private:
    double sampleRate = 48000.0;
    float filtered = 0.0f, smoothedHz = 0.0f, envelope = 0.0f;
    long long currentSampleIndex = 0, lastZeroCross = 0;
    bool isPositive = false;
};