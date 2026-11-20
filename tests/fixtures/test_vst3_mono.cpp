// A VST3 audio effect with one mono input and one mono output, and no other
// layout, as a DPF effect with one input and one output declares itself: its
// output is its input at half level.
//
// Built by the suite as a real .vst3 bundle and hosted through the production
// JUCE adapter.

#include <juce_audio_processors/juce_audio_processors.h>

class TestVst3Mono final : public juce::AudioProcessor {
public:
    TestVst3Mono()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::mono(), true)
                             .withOutput("Output", juce::AudioChannelSet::mono(), true)) {}

    const juce::String getName() const override { return "Blokkily Test VST3 Mono"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono() &&
               layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
    }
    void processBlock(juce::AudioBuffer<float>& block, juce::MidiBuffer&) override {
        getBusBuffer(block, false, 0).applyGain(0, 0, block.getNumSamples(), 0.5F);
    }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestVst3Mono(); }
