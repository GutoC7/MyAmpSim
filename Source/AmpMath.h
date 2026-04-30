#pragma once
#include <cmath>
#include <vector>

class TubeDistortion
{
public:
    static float process(float input, float drive) { return std::tanh(input * drive); }
};

class SimpleOscillator
{
public:
    SimpleOscillator() = default;
    float process(float sampleRate, float frequency)
    {
        float phaseIncrement = 6.28318530718f * frequency / sampleRate;
        currentPhase += phaseIncrement;
        if (currentPhase > 6.28318530718f) currentPhase -= 6.28318530718f;
        return std::sin(currentPhase);
    }
    void reset() { currentPhase = 0.0f; }
private:
    float currentPhase = 0.0f;
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