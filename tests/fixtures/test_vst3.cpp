#include <juce_audio_processors/juce_audio_processors.h>

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
                        2.0, (key_ - 69 + bend_semitones_) / 12.0);
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
                bend_semitones_ = (message.getPitchWheelValue() - 8192) / 8192.0 * 2.0;
            }
            if (message.isNoteOn()) {
                sounding_ = true;
                key_ = message.getNoteNumber();
                phase_ = 0.0;
            }
            if (message.isNoteOff()) sounding_ = false;
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
    int key_ = 60;
    double bend_semitones_ = 0.0;
    double phase_ = 0.0;
    double sample_rate_ = 48000.0;
    juce::AudioParameterFloat* level_ = nullptr;
    juce::AudioParameterFloat* tone_ = nullptr;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestVst3Processor(); }
