// A VST3 audio effect whose work can be read straight off the rendered audio:
// the output is the input, delayed by the 32 samples it reports as its
// latency, inverted and scaled by its Gain parameter (index 0, default 0.25).
// The inversion tells its output apart from the CLAP effect fixture's in a
// chain: CLAP (0.25) into VST3 (-0.25) renders -0.0625. Its tail is the 32
// samples still in its line, and its state is the gain.
//
// Built by the suite as a real .vst3 bundle and hosted through the production
// JUCE adapter.

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstring>

namespace {
constexpr int latency = 32;
}

class TestVst3Effect final : public juce::AudioProcessor {
public:
    TestVst3Effect()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                             .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)) {
        addParameter(gain_ = new juce::AudioParameterFloat({"gain", 1}, "Gain", 0.0F, 1.0F, 0.25F));
        setLatencySamples(latency);
    }

    const juce::String getName() const override { return "Blokkily Test VST3 Effect"; }
    void prepareToPlay(double sample_rate, int) override {
        sample_rate_ = sample_rate;
        for (auto& channel : line_) channel.fill(0.0F);
        key_line_.fill(0.0F);
        cursor_ = 0;
    }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo() ||
            layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;
        if (layouts.inputBuses.size() < 2) return true;
        const auto key = layouts.inputBuses[1];
        return key.isDisabled() || key == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& block, juce::MidiBuffer&) override {
        auto audio = getBusBuffer(block, true, 0);
        const bool keyed = getBusCount(true) > 1 && getBus(true, 1)->isEnabled();
        const auto key = keyed ? getBusBuffer(block, true, 1) : juce::AudioBuffer<float>();
        const int channels = std::min(audio.getNumChannels(), 2);
        for (int frame = 0; frame < audio.getNumSamples(); ++frame) {
            float level = 0.0F;
            for (int channel = 0; channel < key.getNumChannels(); ++channel)
                level = std::max(level, std::abs(key.getSample(channel, frame)));
            const float ducked_by = key_line_[cursor_];
            key_line_[cursor_] = std::min(level, 1.0F);
            const float gain = -gain_->get() * (1.0F - ducked_by);
            for (int channel = 0; channel < channels; ++channel) {
                auto& slot = line_[static_cast<std::size_t>(channel)][cursor_];
                const float delayed = slot;
                slot = audio.getSample(channel, frame);
                audio.setSample(channel, frame, gain * delayed);
            }
            cursor_ = (cursor_ + 1) % static_cast<std::size_t>(latency);
        }
    }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    double getTailLengthSeconds() const override { return latency / sample_rate_; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override {
        const float value = gain_->get();
        destination.setSize(sizeof(value));
        std::memcpy(destination.getData(), &value, sizeof(value));
    }
    void setStateInformation(const void* data, int size) override {
        float value = 0.0F;
        if (size != sizeof(value)) return;
        std::memcpy(&value, data, sizeof(value));
        *gain_ = value;
    }

private:
    juce::AudioParameterFloat* gain_ = nullptr;
    double sample_rate_ = 48000.0;
    std::array<std::array<float, latency>, 2> line_{};
    std::array<float, latency> key_line_{};
    std::size_t cursor_ = 0;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestVst3Effect(); }
