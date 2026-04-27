#include "PluginProcessor.h"
#include "PluginEditor.h"

NoiseManSpaceAudioProcessor::NoiseManSpaceAudioProcessor()
    : AudioProcessor (BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {}

NoiseManSpaceAudioProcessor::~NoiseManSpaceAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout NoiseManSpaceAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterBool>("sync", "Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("time", "Time", juce::NormalisableRange<float>(10.0f, 2000.0f, 0.1f, 0.5f), 300.0f, "ms"));
    
    // 1/64から始まる「短い順」のリスト (全18項目)
    juce::StringArray notes { 
        "1/64", "1/64.", "1/64T", 
        "1/32", "1/32.", "1/32T", 
        "1/16", "1/16.", "1/16T", 
        "1/8",  "1/8.",  "1/8T", 
        "1/4",  "1/4.",  "1/4T", 
        "1/2",  "1/2.",  "1/2T" 
    };
    // デフォルトを 1/4 (インデックス12) に設定
    params.push_back(std::make_unique<juce::AudioParameterChoice>("note", "Note", notes, 12));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>("fb", "Feedback", 0.0f, 0.95f, 0.6f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("state", "Density", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("width", "Width", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mod", "Mod", 0.0f, 1.0f, 0.2f)); 
    
    // 並び順の要件に合わせて HP -> LP の順で定義
    params.push_back(std::make_unique<juce::AudioParameterFloat>("hp", "HP", juce::NormalisableRange<float>(20.f, 2000.f, 1.f, 0.3f), 100.f, "Hz"));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("lp", "LP", juce::NormalisableRange<float>(200.f, 20000.f, 1.f, 0.3f), 8000.f, "Hz"));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>("drywet", "DryWet", 0.0f, 1.0f, 0.5f));
    return { params.begin(), params.end() };
}

void NoiseManSpaceAudioProcessor::prepareToPlay (double sr, int) { engine.setup(sr); }
void NoiseManSpaceAudioProcessor::releaseResources() {}

void NoiseManSpaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    bool sync = *apvts.getRawParameterValue("sync") > 0.5f;
    float tMs = (float)*apvts.getRawParameterValue("time");
    int nIdx = (int)*apvts.getRawParameterValue("note");
    float fb = (float)*apvts.getRawParameterValue("fb"), st = (float)*apvts.getRawParameterValue("state");
    float wd = (float)*apvts.getRawParameterValue("width"), md = (float)*apvts.getRawParameterValue("mod");
    float hpHz = (float)*apvts.getRawParameterValue("hp"), lpHz = (float)*apvts.getRawParameterValue("lp"), dw = (float)*apvts.getRawParameterValue("drywet");

    double tSmpl = (tMs / 1000.0) * getSampleRate();
    if (sync) {
        if (auto* ph = getPlayHead()) {
            juce::AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition(pos) && pos.bpm > 0) {
                double bL = 60.0 / pos.bpm;
                // notes配列の順序に完全対応した倍率リスト (短い順)
                double r[] = {
                    0.0625, 0.09375, 0.04166, // 1/64系
                    0.125,  0.1875,  0.08333, // 1/32系
                    0.25,   0.375,   0.16666, // 1/16系
                    0.5,    0.75,    0.33333, // 1/8系
                    1.0,    1.5,     0.66666, // 1/4系
                    2.0,    3.0,     1.33333  // 1/2系
                };
                tSmpl = bL * r[nIdx] * getSampleRate();
            }
        }
    }
    engine.smoothTime.setTargetValue(tSmpl); engine.smoothWidth.setTargetValue(wd);
    double lpC = juce::jlimit(0.001, 0.99, 2.0 * 3.14159 * lpHz / getSampleRate());
    double hpC = juce::jlimit(0.001, 0.99, 2.0 * 3.14159 * hpHz / getSampleRate());
    double ang = dw * 1.57079, dG = std::cos(ang), wG = std::sin(ang);

    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        double curT = engine.smoothTime.getNextValue(), curW = engine.smoothWidth.getNextValue();
        auto ad = [&](int ch) {
            double x = (double)buffer.getSample(ch, i);
            x = std::tanh(x * 1.02); x = x * 0.92 + engine.adLPPos[ch] * 0.08;
            engine.adLPPos[ch] = x; return x;
        };
        double aL = ad(0), aR = ad(1);
        double wL = engine.processCluster(0, aL, curT, fb, st, lpC, hpC, curW, md);
        double wR = engine.processCluster(1, aR, curT, fb, st, lpC, hpC, curW, md);
        double m = (wL + wR) * 0.5, s = (wL - wR) * 0.5, sG = 1.0 + (curW * 4.0), cp = 1.0 / std::sqrt(1.0 + (curW * curW * 2.0));
        double fWL = (m + s * sG) * cp, fWR = (m - s * sG) * cp;
        auto da = [&](double in, int ch) {
            double x = std::floor(in * 32768.0) / 32768.0;
            x = x * 0.88 + engine.daLPPos[ch] * 0.12; engine.daLPPos[ch] = x;
            x += (engine.rng.nextDouble() - 0.5) * 0.000002; return (float)x;
        };
        buffer.setSample(0, i, da(aL * dG + fWL * wG, 0));
        if (buffer.getNumChannels() > 1) buffer.setSample(1, i, da(aR * dG + fWR * wG, 1));
        engine.advance();
    }
}
void NoiseManSpaceAudioProcessor::getStateInformation (juce::MemoryBlock& d) { auto xml = apvts.copyState().createXml(); copyXmlToBinary(*xml, d); }
void NoiseManSpaceAudioProcessor::setStateInformation (const void* d, int s) { auto xml = getXmlFromBinary(d, s); if (xml) apvts.replaceState(juce::ValueTree::fromXml(*xml)); }
bool NoiseManSpaceAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* NoiseManSpaceAudioProcessor::createEditor() { return new NoiseManSpaceAudioProcessorEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NoiseManSpaceAudioProcessor(); }