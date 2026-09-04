#include <juce_audio_processors/juce_audio_processors.h>

class TestVst3Processor final : public juce::AudioProcessor {
public:
    TestVst3Processor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        addParameter(level_ = new juce::AudioParameterFloat({"level", 1}, "Level", 0.0F, 1.0F, 0.25F));
    }

    const juce::String getName() const override { return "Blokkily Test VST3"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        audio.clear();
        int cursor = 0;
        const auto render = [&](int end) {
            for (; cursor < end; ++cursor)
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    audio.setSample(channel, cursor, sounding_ ? level_->get() : 0.0F);
        };
        for (const auto metadata : midi) {
            render(metadata.samplePosition);
            if (metadata.getMessage().isNoteOn()) sounding_ = true;
            if (metadata.getMessage().isNoteOff()) sounding_ = false;
        }
        render(audio.getNumSamples());
    }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override {
        const float value = level_->get();
        destination.setSize(sizeof(value));
        std::memcpy(destination.getData(), &value, sizeof(value));
    }
    void setStateInformation(const void* data, int size) override {
        float value = 0.0F;
        if (size != sizeof(value)) return;
        std::memcpy(&value, data, sizeof(value));
        *level_ = value;
    }

private:
    bool sounding_ = false;
    juce::AudioParameterFloat* level_ = nullptr;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestVst3Processor(); }
