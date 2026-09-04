#include "blokkily/model/pattern.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <iostream>

int main() {
    blokkily::Pattern pattern;

    blokkily::Trigger kick;
    kick.start = 0;
    kick.duration = 120;
    kick.musical_data = blokkily::Note{36, 1.0F, 0.0F};
    (void)pattern.add(kick);

    blokkily::Trigger chord;
    chord.start = 480;
    chord.duration = 420;
    chord.musical_data = blokkily::Chord{60, {0, 3, 7, 10}, 1, 12};
    chord.ratchets = 1;
    (void)pattern.add(chord);

    const blokkily::Scheduler scheduler;
    std::cout << "blokkily 0.1 — unified pattern proof\n";
    for (const auto& note : scheduler.render_loop(pattern, 1, 0xB10CCULL)) {
        std::cout << "tick=" << note.start << " key=" << note.key
                  << " duration=" << note.duration << '\n';
    }
}

