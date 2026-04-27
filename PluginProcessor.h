#pragma once
#include <JuceHeader.h>

class NoiseManSpaceAudioProcessor  : public juce::AudioProcessor
{
public:
    NoiseManSpaceAudioProcessor();
    ~NoiseManSpaceAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "NoiseManSpace"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override {}
    const juce::String getProgramName (int index) override { return {}; }
    void changeProgramName (int index, const juce::String& newName) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct AllPass {
        std::vector<double> buffer;
        int pos = 0;
        double process(double in, double m) {
            if (buffer.empty()) return in;
            double bufOut = buffer[pos];
            double out = -m * in + bufOut;
            buffer[pos] = in + m * out;
            pos = (pos + 1) % (int)buffer.size();
            return out;
        }
    };

    struct InternalEngine {
        juce::AudioBuffer<double> mainDelay; 
        int writePos = 0;
        AllPass diffusers[2][4];
        double lp[2] = {0, 0}, f_lp[2] = {0, 0}, f_hp[2] = {0, 0};
        juce::LinearSmoothedValue<double> smoothWidth { 0.0 }, smoothTime { 0.0 };
        double adLPPos[2] = {0, 0}, daLPPos[2] = {0, 0};
        juce::Random rng;
        double lfoPhase = 0.0, lfoStep = 0.0;

        void setup(double sr) {
            mainDelay.setSize(2, (int)(sr * 4.0));
            mainDelay.clear();
            int sL[4] = { 1511, 2341, 3571, 4861 }, sR[4] = { 1693, 2593, 3853, 5101 };
            for(int i=0; i<4; ++i) {
                diffusers[0][i].buffer.assign(sL[i], 0.0);
                diffusers[1][i].buffer.assign(sR[i], 0.0);
            }
            smoothWidth.reset(sr, 0.05);
            smoothTime.reset(sr, 0.12);
            lfoStep = (2.0 * juce::MathConstants<double>::pi * 0.4) / sr;
            std::fill(adLPPos, adLPPos + 2, 0.0); std::fill(daLPPos, daLPPos + 2, 0.0);
        }

        double processCluster(int chan, double input, double tSmpl, double fb, double state, double lpC, double hpC, double width, double mod) {
            auto* data = mainDelay.getWritePointer(chan);
            int bufSize = mainDelay.getNumSamples();
            double lfo = std::sin(lfoPhase + (chan == 1 ? 1.57 : 0.0)) * mod * 80.0;
            double rPos = (double)writePos - (tSmpl + (chan == 1 ? width * 3000.0 : 0.0) + lfo);
            while (rPos < 0) rPos += (double)bufSize;
            int idx1 = (int)rPos % bufSize, idx2 = (idx1 + 1) % bufSize;
            double frac = rPos - (int)rPos;
            double delayed = mainDelay.getSample(chan, idx1) * (1.0 - frac) + mainDelay.getSample(chan, idx2) * frac;

            double dOut = delayed;
            for(int i=0; i<4; ++i) dOut = diffusers[chan][i].process(dOut, 0.65 + (state * 0.15));
            lp[chan] += (0.2 + state * 0.5) * (dOut - lp[chan]);
            double wet = (delayed * (1.0 - state)) + (lp[chan] * state);
            f_lp[chan] += lpC * (wet - f_lp[chan]);
            f_hp[chan] += hpC * (f_lp[chan] - f_hp[chan]);
            double flt = f_lp[chan] - f_hp[chan];
            flt *= (1.0 + (rng.nextDouble() - 0.5) * 0.00001);
            data[writePos] = input + (flt * fb);
            return flt;
        }
        void advance() { 
            writePos = (writePos + 1) % mainDelay.getNumSamples(); 
            lfoPhase = std::fmod(lfoPhase + lfoStep, juce::MathConstants<double>::twoPi);
        }
    } engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseManSpaceAudioProcessor)
};