// The controller's side of the metronome and count-in (item 3.7). The
// settings live in the song (SongModel reaches the engine through applyMix);
// Play starts a count-in when recording is armed (togglePlayback), and the
// transport is told while one plays.

#include "app_controller.hpp"

void AppController::pollCountIn() {
    const bool counting = engine_ != nullptr && engine_->counting_in();
    if (counting == counting_in_) return;
    counting_in_ = counting;
    emit countInChanged();
}
