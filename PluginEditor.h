#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class NoiseManSpaceAudioProcessorEditor  : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    NoiseManSpaceAudioProcessorEditor (NoiseManSpaceAudioProcessor&);
    ~NoiseManSpaceAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    NoiseManSpaceAudioProcessor& audioProcessor;
    juce::Slider timeS, noteS, fbS, stS, wdS, mdS, lpS, hpS, dwS;
    juce::ToggleButton syncB;
    juce::Label timeL, fbL, stL, wdL, mdL, lpL, hpL, dwL;

    using Att = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Att> tA, nA, fA, sA, wA, mA, lA, hA, dA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseManSpaceAudioProcessorEditor)
};