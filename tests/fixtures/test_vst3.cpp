#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <mutex>
#include <vector>

class TestVst3Processor;
namespace {
// Every live processor, so that the exported hook below can reach them the
// way the plugin's own editor would: from the plugin's message thread.
std::mutex live_mutex;
std::vector<TestVst3Processor*> live_processors;
}

class TestVst3Processor final : public juce::AudioProcessor {
public:
    TestVst3Processor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        addParameter(level_ = new juce::AudioParameterFloat({"level", 1}, "Level", 0.0F, 1.0F, 0.25F));
        // At rest the fixture holds a steady level, which is what the timing
        // and mixer gates measure. Asked for a tone it becomes a real
        // oscillator at the pitch it was played at, bend included, so a
        // retuned note can be proved from the audio itself.
        addParameter(tone_ = new juce::AudioParameterFloat({"tone", 1}, "Tone", 0.0F, 1.0F, 0.0F));
        const std::lock_guard lock(live_mutex);
        live_processors.push_back(this);
    }
    ~TestVst3Processor() override {
        const std::lock_guard lock(live_mutex);
        live_processors.erase(std::remove(live_processors.begin(), live_processors.end(), this),
                              live_processors.end());
    }

    // What a knob turn in the plugin's own window does: one gesture around a
    // value change, which the JUCE wrapper announces to the host through its
    // IComponentHandler (beginEdit, performEdit, endEdit).
    void turn_level(float value) {
        level_->beginChangeGesture();
        *level_ = value;
        level_->endChangeGesture();
    }

    const juce::String getName() const override { return "Blokkily Test VST3"; }
    void prepareToPlay(double sample_rate, int) override { sample_rate_ = sample_rate; }
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        audio.clear();
        int cursor = 0;
        const auto render = [&](int end) {
            for (; cursor < end; ++cursor) {
                float sample = 0.0F;
                if (sounding_ && tone_->get() >= 0.5F) {
                    const double frequency = 440.0 * std::pow(
                        2.0, (key_ - 69 + bend_semitones_[static_cast<std::size_t>(channel_)]) / 12.0);
                    sample = static_cast<float>(level_->get() * std::sin(phase_));
                    phase_ += juce::MathConstants<double>::twoPi * frequency / sample_rate_;
                    if (phase_ > juce::MathConstants<double>::twoPi)
                        phase_ -= juce::MathConstants<double>::twoPi;
                } else if (sounding_) {
                    sample = level_->get();
                }
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    audio.setSample(channel, cursor, sample);
            }
        };
        for (const auto metadata : midi) {
            render(metadata.samplePosition);
            const auto message = metadata.getMessage();
            if (message.isPitchWheel()) {
                // The host announces a two-semitone bend range on every voice
                // channel, so that is what a wheel value means here.
                // Each channel has its wheel, as an MPE voice does; a note
                // follows the wheel of the channel it was struck on.
                bend_semitones_[static_cast<std::size_t>(message.getChannel())] =
                    (message.getPitchWheelValue() - 8192) / 8192.0 * 2.0;
            }
            if (message.isNoteOn()) {
                sounding_ = true;
                key_ = message.getNoteNumber();
                channel_ = message.getChannel();
                phase_ = 0.0;
            }
            if (message.isNoteOff()) sounding_ = false;
        }
        render(audio.getNumSamples());
    }

    // The editor fills 320 x 200 with one colour, #C8FF3C, so a grab of the
    // host window proves it reached the screen: the centre pixel is that
    // colour or the editor is not there.
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
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
    int key_ = 60;
    std::array<double, 17> bend_semitones_{};
    int channel_ = 1;
    double phase_ = 0.0;
    double sample_rate_ = 48000.0;
    juce::AudioParameterFloat* level_ = nullptr;
    juce::AudioParameterFloat* tone_ = nullptr;
};

namespace {
struct TestEditor final : juce::AudioProcessorEditor {
    explicit TestEditor(juce::AudioProcessor& processor) : AudioProcessorEditor(processor) {
        setOpaque(true);
        setSize(320, 200);
    }
    void paint(juce::Graphics& graphics) override { graphics.fillAll(juce::Colour(0xffc8ff3cU)); }
};
} // namespace

juce::AudioProcessorEditor* TestVst3Processor::createEditor() { return new TestEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestVst3Processor(); }

// Turns the Level knob of every live instance as the plugin's editor would.
// The wrapper only talks to the host's component handler from the plugin's
// message thread, so the turn runs there: at once when the caller is that
// thread (a host whose main thread the plugin adopted, as a headless host's
// is), otherwise posted to it, in which case it arrives asynchronously.
extern "C" __attribute__((visibility("default"))) void blokkily_test_vst3_turn(float value) {
    const auto turn = [value] {
        const std::lock_guard lock(live_mutex);
        for (auto* processor : live_processors) processor->turn_level(value);
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) turn();
    else juce::MessageManager::callAsync(turn);
}
