#include "PluginProcessor.h"

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()) {
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 1; i <= 5; ++i) {
        auto pfx = "B" + juce::String(i) + "_";
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pfx + "Thresh", pfx + "Thresh", -30.0f, 30.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pfx + "Ratio", pfx + "Ratio", 1.5f, 20.0f, 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pfx + "Attack", pfx + "Attack", 
            juce::StringArray{ "0.1ms", "0.3ms", "1ms", "3ms", "10ms", "30ms" }, 5));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pfx + "Release", pfx + "Release", 
            juce::StringArray{ "0.1s", "0.3s", "0.6s", "1.2s", "Auto" }, 4));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pfx + "Gain", pfx + "Gain", -30.0f, 30.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterBool>(pfx + "Bypass", pfx + "Bypass", false));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>("XOver1", "XOver1", 50.0f, 300.0f, 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("XOver2", "XOver2", 250.0f, 1500.0f, 650.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("XOver3", "XOver3", 1000.0f, 6000.0f, 2500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("XOver4", "XOver4", 4000.0f, 16000.0f, 8000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("Lim_Thresh", "Lim Thresh", -12.0f, 0.0f, -0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("Lim_Ceiling", "Lim Ceiling", -6.0f, 0.0f, -0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("Lim_Release", "Lim Release", 5.0f, 500.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("Lim_Enable", "Lim Enable", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("OutputGain", "Output Gain", -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("Mix", "Mix", 0.0f, 100.0f, 100.0f));

    return { params.begin(), params.end() };
}

void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    for (auto& b : bands) b.reset(sampleRate);
    oversampler->initProcessing(samplesPerBlock);
    limGain = 1.0f;
}

void AudioPluginAudioProcessor::releaseResources() {
    oversampler->reset();
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    auto numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    // Обновляем частоты среза кроссоверов
    float x1 = apvts.getRawParameterValue("XOver1")->load();
    float x2 = apvts.getRawParameterValue("XOver2")->load();
    float x3 = apvts.getRawParameterValue("XOver3")->load();
    float x4 = apvts.getRawParameterValue("XOver4")->load();

    xo1L.setCutoffFrequency(x1); xo1R.setCutoffFrequency(x1);
    xo2L.setCutoffFrequency(x2); xo2R.setCutoffFrequency(x2);
    xo3L.setCutoffFrequency(x3); xo3R.setCutoffFrequency(x3);
    xo4L.setCutoffFrequency(x4); xo4R.setCutoffFrequency(x4);

    float mix = apvts.getRawParameterValue("Mix")->load() * 0.01f;
    float outGainLin = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("OutputGain")->load());

    for (int i = 0; i < numSamples; ++i) {
        float inL = left[i];
        float inR = right[i];

        // Дерево кроссоверов LR4
        float lowHalfL = 0, highHalfL = 0, lowHalfR = 0, highHalfR = 0;
        xo2L.processSample(0, inL, lowHalfL, highHalfL);
        xo2R.processSample(0, inR, lowHalfR, highHalfR);

        float b1L = 0, b2L = 0, b1R = 0, b2R = 0;
        xo1L.processSample(0, lowHalfL, b1L, b2L);
        xo1R.processSample(0, lowHalfR, b1R, b2R);

        float b3L = 0, upperL = 0, b3R = 0, upperR = 0;
        xo3L.processSample(0, highHalfL, b3L, upperL);
        xo3R.processSample(0, highHalfR, b3R, upperR);

        float b4L = 0, b5L = 0, b4R = 0, b5R = 0;
        xo4L.processSample(0, upperL, b4L, b5L);
        xo4R.processSample(0, upperR, b4R, b5R);

        float bInL[5] = { b1L, b2L, b3L, b4L, b5L };
        float bInR[5] = { b1R, b2R, b3R, b4R, b5R };
        float sumL = 0.0f, sumR = 0.0f;

        for (int b = 0; b < 5; ++b) {
            auto pfx = "B" + juce::String(b + 1) + "_";
            bool bypass = apvts.getRawParameterValue(pfx + "Bypass")->load() > 0.5f;
            float th = apvts.getRawParameterValue(pfx + "Thresh")->load();
            float rat = apvts.getRawParameterValue(pfx + "Ratio")->load();
            int att = (int)apvts.getRawParameterValue(pfx + "Attack")->load();
            int rel = (int)apvts.getRawParameterValue(pfx + "Release")->load();
            float gn = juce::Decibels::decibelsToGain(apvts.getRawParameterValue(pfx + "Gain")->load());

            float g = bypass ? 1.0f : bands[b].process(bInL[b], bInR[b], th, rat, att, rel);
            sumL += bInL[b] * g * gn;
            sumR += bInR[b] * g * gn;
        }

        left[i] = juce::jmap(mix, inL, sumL * outGainLin);
        right[i] = juce::jmap(mix, inR, sumR * outGainLin);
    }

    // 4x True Peak Limiter
    bool limOn = apvts.getRawParameterValue("Lim_Enable")->load() > 0.5f;
    if (limOn) {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampled = oversampler->processSamplesUp(block);

        float limThreshLin = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("Lim_Thresh")->load());
        float limCeilLin = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("Lim_Ceiling")->load());
        float limRelMs = apvts.getRawParameterValue("Lim_Release")->load();
        float relCoeff = std::exp(-1.0f / (limRelMs * 0.001f * (float)(getSampleRate() * 4.0)));

        auto* osL = oversampled.getChannelPointer(0);
        auto* osR = oversampled.getChannelPointer(1);

        for (size_t i = 0; i < oversampled.getNumSamples(); ++i) {
            float peak = std::max(std::abs(osL[i]), std::abs(osR[i]));
            float target = peak > limThreshLin ? (limCeilLin / (peak + 1e-5f)) : 1.0f;
            target = std::min(1.0f, target);

            if (target < limGain) limGain = target;
            else limGain = target + relCoeff * (limGain - target);

            osL[i] = juce::jlimit(-limCeilLin, limCeilLin, osL[i] * limGain);
            osR[i] = juce::jlimit(-limCeilLin, limCeilLin, osR[i] * limGain);
        }

        oversampler->processSamplesDown(block);
    }
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AudioPluginAudioProcessor();
}