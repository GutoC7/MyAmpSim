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

class PitchTracker
{
public:
    PitchTracker() { std::fill(buffer, buffer + 2048, 0.0f); }

    void prepare(double sr) { sampleRate = sr; }

    void process(float sample)
    {
        // 1. Track the volume envelope to act as a noise gate
        env = env * 0.99f + std::abs(sample) * 0.01f;

        buffer[writeIdx++] = sample;

        // 2. Once we collect enough samples, run the AMDF analysis
        if (writeIdx >= 2048) {
            if (env > 0.01f) computeAMDF();
            else smoothedHz = 0.0f; // Gate closed

            // Shift the second half of the buffer to the front to overlap the analysis
            std::memmove(buffer, buffer + 1024, 1024 * sizeof(float));
            writeIdx = 1024;
        }
    }

    float getHz() const { return smoothedHz; }

private:
    double sampleRate = 48000.0;
    float buffer[2048];
    int writeIdx = 0;
    float smoothedHz = 0.0f, env = 0.0f;

    void computeAMDF()
    {
        // Search range: ~60 Hz (Low B string) to ~1000 Hz (High frets)
        int minLag = static_cast<int>(sampleRate / 1000.0f);
        int maxLag = static_cast<int>(sampleRate / 60.0f);

        float minVal = 1e9f;
        int bestLag = -1;

        // The AMDF Algorithm: Slide the waveform over itself and measure the difference
        for (int lag = minLag; lag <= maxLag; ++lag) {
            float sum = 0.0f;
            for (int i = 0; i < 1024; ++i) {
                sum += std::abs(buffer[i] - buffer[i + lag]);
            }
            // The lag with the lowest difference is the true fundamental period
            if (sum < minVal) {
                minVal = sum;
                bestLag = lag;
            }
        }

        if (bestLag > 0) {
            float hz = sampleRate / static_cast<float>(bestLag);
            // Smooth the output so the synth doesn't warble
            smoothedHz = smoothedHz * 0.8f + hz * 0.2f;
        }
    }
};