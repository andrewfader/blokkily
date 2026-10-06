#include "blokkily/project/midi_file.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() try {
    // features/sequencer_workflow.feature: export the scheduled arrangement.
    Song song;
    song.tracks.resize(2);
    song.tracks[0].name = "Keys";
    song.tracks[1].name = "Drums";
    const auto section = song.add_section("Verse", 1920, 480);
    Trigger note;
    note.start = 120; note.duration = 360; note.micro_offset = 13;
    note.musical_data = Note{64, 0.75F};
    (void)song.patterns[*song.part(section, 0)].pattern.add(note);
    note.start = 0; note.musical_data = Note{36, 1.0F};
    (void)song.patterns[*song.part(section, 1)].pattern.add(note);
    (void)song.place_section(section, 0);
    (void)song.place_section(section, 3840);
    song.tempo.set({0, 110.0, true});
    song.tempo.set({960, 140.0, false});
    song.meter.changes.push_back({2, 3, 4});
    song.patterns[*song.part(section, 0)].pattern.add_continuous(
        {180, ContinuousEvent::Kind::pitch_bend, 0, 10000});
    const auto path = std::filesystem::current_path() / "midi-roundtrip.mid";
    std::string error;
    require(write_midi_file(path, song, &error), "write MIDI");
    const auto file = read_midi_file(path, &error);
    std::filesystem::remove(path);
    require(file.has_value(), "read written MIDI");
    require(file->tracks.size() == 3 && file->ticks_per_quarter == 480, "conductor and tracks");
    require(file->tempos.size() == 9 && std::abs(file->tempos.front().bpm - 110.0) < 0.001 &&
            std::abs(file->tempos.back().bpm - 140.0) < 0.001, "tempo ramp samples");
    require(file->meters.size() == 2 && file->meters[1].at == 3840 &&
            file->meters[1].numerator == 3, "meter change");
    require(file->tracks[1].controls.size() == 2 &&
            file->tracks[1].controls[0].event.tick == 180 &&
            file->tracks[1].controls[0].event.value == 10000, "exported pitch bend");
    for (std::size_t track = 0; track < 2; ++track) {
        const auto scheduled = song.arrange(track, 0);
        const auto& exported = file->tracks[track + 1];
        require(exported.name == song.tracks[track].name, "track name");
        require(exported.notes.size() == scheduled.notes.size(), "scheduled note count");
        for (std::size_t i = 0; i < scheduled.notes.size(); ++i) {
            const auto& a = scheduled.notes[i]; const auto& b = exported.notes[i];
            require(a.start == b.start && a.duration == b.duration && a.key == b.key,
                    "scheduled pitch and timing");
            require(std::abs(a.velocity - b.velocity / 127.0F) < 0.008F, "scheduled velocity");
            require(b.channel == (track == 1 ? 9 : 0), "MIDI channels");
        }
    }
    Song imported;
    const auto report = import_midi_file(imported, *file, "Imported");
    require(report && report->notes == 4 && imported.tracks.size() == 2, "import tracks and notes");
    for (std::size_t track = 0; track < 2; ++track) {
        const auto before = song.arrange(track, 0).notes;
        const auto after = imported.arrange(track, 0).notes;
        require(before.size() == after.size(), "imported scheduled note count");
        for (std::size_t i = 0; i < before.size(); ++i)
            require(before[i].start + report->start == after[i].start && before[i].duration == after[i].duration &&
                    before[i].key == after[i].key, "imported timing and pitch");
    }
    // An independent format-0 byte fixture uses running status and velocity-zero release.
    const std::vector<std::uint8_t> fixture{
        'M','T','h','d',0,0,0,6,0,0,0,1,1,0xE0,
        'M','T','r','k',0,0,0,15,
        0,0x90,60,100, 0x83,0x60,60,0, 0,0xFF,0x2F,0, 0,0,0};
    const auto decoded = decode_midi_file(fixture, &error);
    require(decoded && decoded->tracks[0].notes.size() == 1 &&
            decoded->tracks[0].notes[0].duration == 480, "running status and zero release");
    auto truncated = encode_midi_file(song);
    truncated.resize(14);
    require(!decode_midi_file(truncated), "reject missing tracks");
    truncated = fixture; truncated.pop_back();
    require(!decode_midi_file(truncated), "reject truncated chunk");
    auto invalid = fixture; invalid[25] = 0xF1;
    require(!decode_midi_file(invalid), "reject invalid status");
    MidiFile multi;
    multi.tracks.resize(1);
    multi.tracks[0].notes = {{0,120,0,60,100}, {0,120,9,36,100}};
    multi.tracks[0].controls.push_back({0, {24000, ContinuousEvent::Kind::control_change, 1, 99}});
    Song channel_song;
    const auto channel_report = import_midi_file(channel_song, multi, "Channels");
    require(channel_report && channel_song.tracks.size() == 2, "split format zero channels");
    require(channel_song.arrange(0,0).notes[0].key == 60 &&
            channel_song.arrange(1,0).notes[0].key == 36, "separate imported instruments");
    require(channel_song.arrange(0,0).continuous.size() == 1 &&
            channel_song.arrange(0,0).continuous[0].start == 24000 + channel_report->start,
            "controllers after the final note survive sparse import");
    std::cout << "MIDI file scenarios passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
