#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class SSLBandCompressor {
public:
    void reset(double sampleRate) {
        sr = sampleRate;
        envDb = -120.0f;
        autoSlowDb = -120.0f;
        grDb = 0.0f;
    }

    float process(float inL, float inR, float threshDb, float ratio, int attIdx, int relIdx) {
        float detLin = std::max(std::abs(inL), std::abs(inR));
        float detDb = detLin > 1e-6f ? juce::Decibels::gainToDecibels(detLin) : -120.0f;

        static const float attTimes[] = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
        float attMs = attTimes[juce::jlimit(0, 5, attIdx)];
        float attCoeff = std::exp(-1.0f / (attMs * 0.001f * (float)sr));

        if (detDb > envDb) {
            envDb = detDb + attCoeff * (envDb - detDb);
        } else {
            if (relIdx == 4) { // Auto
                float rFast = std::exp(-1.0f / (0.080f * (float)sr));
                float rSlow = std::exp(-1.0f / (1.000f * (float)sr));
                autoSlowDb = detDb + rSlow * (autoSlowDb - detDb);
                float target = std::max(detDb, autoSlowDb);
                envDb = target + rFast * (envDb - target);
            } else {
                static const float relTimes[] = { 100.0f, 300.0f, 600.0f, 1200.0f };
                float relMs = relTimes[juce::jlimit(0, 3, relIdx)];
                float rCoeff = std::exp(-1.0f / (relMs * 0.001f * (float)sr));
                envDb = detDb + rCoeff * (envDb - detDb);
            }
        }

        float delta = envDb - threshDb;
        float halfKnee = 2.0f; // 4 dB soft knee
        float gainRed = 0.0f;
        float safeRatio = juce::jlimit(1.5f, 20.0f, ratio);

        if (delta <= -halfKnee) {
            gainRed = 0.0f;
        } else if (delta >= halfKnee) {
            gainRed = delta * (1.0f - 1.0f / safeRatio);
        } else {
            float k = (delta + halfKnee) / 4.0f;
            gainRed = (k * k * delta) * (1.0f - 1.0f / safeRatio);
        }

        grDb = -gainRed;
        return juce::Decibels::decibelsToGain(-gainRed);
    }

    float getGR() const { return grDb; }

private:
    double sr = 44100.0;
    float envDb = -120.0f;
    float autoSlowDb = -120.0f;
    float grDb = 0.0f;
};

class AudioPluginAudioProcessor : public juce::AudioProcessor {
public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SSL 5-Band Compressor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // 4 Linkwitz-Riley Crossovers (LR4 / 24dB/oct)
    juce::dsp::LinkwitzRileyFilter<float> xo1L, xo1R, xo2L, xo2R, xo3L, xo3R, xo4L, xo4R;
    SSLBandCompressor bands[5];

    // True Peak Limiter Oversampling (4x)
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    float limGain = 1.0f;
};