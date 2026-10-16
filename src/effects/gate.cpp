// A noise gate. It opens when the louder channel (or, keyed, the sidechain
// key) rises above the threshold, stays open for the hold time after the
// level last was above it, and closes when the level falls 3 dB under the
// threshold (the hysteresis keeps a level hovering at the threshold from
// chattering). Opening ramps the gain up over the attack time and closing
// ramps it down over the release time, to the range: -80 dB is shut, -10 dB
// only ducks. The gain moves in decibels, so a release sounds even.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 5> specs{{
    {"Threshold", -80.0, 0.0, -40.0},
    {"Attack", 0.01, 50.0, 0.5},
    {"Hold", 0.0, 500.0, 20.0},
    {"Release", 5.0, 2000.0, 100.0},
    {"Range", -80.0, 0.0, -80.0},
}};

constexpr double hysteresis_db = 3.0;

class Gate final : public BuiltinEffect {
public:
    Gate() : BuiltinEffect("gate", specs) {}

    std::uint64_t tail_samples() const noexcept override { return 0; }

protected:
    void prepare(double) override { clear(); }

    void clear() noexcept override {
        open_ = false;
        held_ = 0;
        gain_db_ = -1.0e9; // set to the range once update() knows it
    }

    void update() noexcept override {
        const double rate = sample_rate();
        open_level_ = std::pow(10.0, value(gate::threshold_db) / 20.0);
        close_level_ = std::pow(10.0, (value(gate::threshold_db) - hysteresis_db) / 20.0);
        range_db_ = value(gate::range_db);
        // Decibels per sample: the whole range in the attack or release time.
        const double span = std::max(1.0, -range_db_);
        attack_step_ = span / std::max(1.0, value(gate::attack_ms) / 1000.0 * rate);
        release_step_ = span / std::max(1.0, value(gate::release_ms) / 1000.0 * rate);
        hold_ = static_cast<std::uint64_t>(value(gate::hold_ms) / 1000.0 * rate);
        if (gain_db_ < range_db_) gain_db_ = range_db_;
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        const bool keyed = key_.left.size() >= frames && key_.right.size() >= frames;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double level = keyed
                ? std::max(std::abs(key_.left[frame]), std::abs(key_.right[frame]))
                : std::max(std::abs(left[frame]), std::abs(right[frame]));
            if (level >= open_level_) {
                open_ = true;
                held_ = hold_;
            } else if (open_ && level < close_level_) {
                if (held_ > 0) --held_;
                else open_ = false;
            }
            gain_db_ = open_ ? std::min(0.0, gain_db_ + attack_step_)
                             : std::max(range_db_, gain_db_ - release_step_);
            if (gain_db_ >= 0.0) continue; // open: untouched
            const auto gain = static_cast<float>(std::pow(10.0, gain_db_ / 20.0));
            left[frame] *= gain;
            right[frame] *= gain;
        }
    }

private:
    bool open_ = false;
    std::uint64_t held_ = 0, hold_ = 0;
    double open_level_ = 0.01, close_level_ = 0.007, range_db_ = -80.0;
    double attack_step_ = 1.0, release_step_ = 0.01, gain_db_ = -80.0;
};

} // namespace

std::unique_ptr<PluginInstance> make_gate() { return std::make_unique<Gate>(); }

} // namespace blokkily::effects
