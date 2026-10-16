// The LV2 fixture (features/lv2_hosting.feature): a real .lv2 bundle the suite
// builds with JUCE and the host loads through its LV2 adapter. While a note
// is held it sounds a steady Level x velocity on both channels, sample
// accurately from the note's offset, so what the host sent can be read off
// the audio; its Level is a parameter and part of its state.

#include <juce_audio_processors/juce_audio_processors.h>

class TestLv2Processor final : public juce::AudioProcessor {
public:
    TestLv2Processor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        addParameter(level_ = new juce::AudioParameterFloat({"level", 1}, "Level", 0.0F, 1.0F, 0.25F));
    }

    const juce::String getName() const override { return "Blokkily Test LV2"; }
    void prepareToPlay(double, int) override { velocity_ = 0.0F; held_ = -1; }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& block, juce::MidiBuffer& midi) override {
        int cursor = 0;
        const auto render_to = [&](int end) {
            const float value = held_ >= 0 ? level_->get() * velocity_ : 0.0F;
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int frame = cursor; frame < end; ++frame) block.setSample(channel, frame, value);
            cursor = end;
        };
        for (const auto metadata : midi) {
            render_to(std::clamp(metadata.samplePosition, 0, block.getNumSamples()));
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                held_ = message.getNoteNumber();
                velocity_ = message.getFloatVelocity();
            } else if (message.isNoteOff() && message.getNoteNumber() == held_) {
                held_ = -1;
            }
        }
        render_to(block.getNumSamples());
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override {
        juce::MemoryOutputStream out(destination, false);
        out.writeString("blokkily-test-lv2");
        out.writeFloat(level_->get());
    }
    void setStateInformation(const void* data, int size) override {
        juce::MemoryInputStream in(data, static_cast<std::size_t>(size), false);
        if (in.readString() != "blokkily-test-lv2") return;
        *level_ = in.readFloat();
    }

private:
    juce::AudioParameterFloat* level_ = nullptr;
    float velocity_ = 0.0F;
    int held_ = -1;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestLv2Processor(); }
