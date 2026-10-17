// Guide unit tests: event bus, narrator, action dispatcher, lesson engine,
// LLM client. No network, no key, no window — the fake transport answers.

#include "blokkily/guide/action_dispatcher.hpp"
#include "blokkily/guide/event_bus.hpp"
#include "blokkily/guide/lesson.hpp"
#include "blokkily/guide/llm_client.hpp"
#include "blokkily/guide/narrator.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace blokkily::guide;

namespace {

void test_event_bus() {
    EventBus bus;
    int seen = 0;
    EventSource last_source = EventSource::user;
    const auto token = bus.subscribe([&](const AppEvent& e) {
        ++seen;
        last_source = e.source;
    });
    bus.publish(AppEvent{EventSource::user, EventKind::note_added, "Note C4", ""});
    bus.publish(AppEvent{EventSource::mcp, EventKind::transport_started, "play", ""});
    assert(seen == 2);
    assert(last_source == EventSource::mcp);
    assert(bus.size() == 2);
    const auto tail = bus.tail(10);
    assert(tail.size() == 2);
    assert(tail[0].seq + 1 == tail[1].seq); // append-only ordering
    bus.unsubscribe(token);
    bus.publish(AppEvent{});
    assert(seen == 2); // listener gone
    std::cout << "event_bus ok\n";
}

void test_narrator() {
    Narrator narrator;
    assert(Narrator::note_name(60) == "C4");
    assert(Narrator::note_name(61) == "C#4");
    assert(Narrator::note_name(36) == "C2");

    SongSnapshot snap;
    snap.note_count = 0;
    snap.song_note_count = 0;
    // Grounded: a song with no notes + play = silence warning, not canned cheer.
    AppEvent play{EventSource::user, EventKind::transport_started, "", ""};
    const auto silent = narrator.describe(play, snap);
    assert(silent.find("silence") != std::string::npos);
    // Another track may well be playing while the open pattern is empty.
    snap.song_note_count = 3;
    const auto playing = narrator.describe(play, snap);
    assert(playing.find("silence") == std::string::npos);

    // The source is always named: user vs mcp vs guide.
    AppEvent mcp_note{EventSource::mcp, EventKind::note_added, "Note E4 at step 4", ""};
    assert(narrator.describe(mcp_note, snap).rfind("Your agent", 0) == 0);
    AppEvent user_note{EventSource::user, EventKind::note_added, "Note E4 at step 4", ""};
    assert(narrator.describe(user_note, snap).rfind("You", 0) == 0);

    // Tempo explanation references the actual BPM.
    snap.tempo_bpm = 140;
    AppEvent tempo{EventSource::user, EventKind::tempo_changed, "", ""};
    assert(narrator.describe(tempo, snap).find("140") != std::string::npos);

    // Theory answers.
    const auto triad = narrator.answer_question("what is a minor triad and why does it sound sad?", snap);
    assert(triad.find("minor third") != std::string::npos);
    const auto cadence = narrator.answer_question("explain a perfect cadence", snap);
    assert(cadence.find("V") != std::string::npos && cadence.find("tonic") != std::string::npos);
    const auto penta = narrator.answer_question("what is the pentatonic scale?", snap);
    assert(penta.find("five notes") != std::string::npos);
    // Unknown question: empty, so the caller can try the LLM or say so.
    assert(narrator.answer_question("what is the airspeed velocity of an unladen swallow?", snap).empty());
    std::cout << "narrator ok\n";
}

struct FakeSink : ICommandSink {
    int added_step = -1, added_key = -1;
    int chord_step = -1, chord_root = -1;
    std::vector<std::int16_t> chord_intervals;
    int removed_step = -1;
    double tempo = 0;
    int play_toggles = 0;
    int tracks_added = 0;
    int instrument = -1;
    int transposed = 0;
    bool cleared = false;
    bool addNote(int step, int key) override {
        added_step = step;
        added_key = key;
        return step >= 0 && step < 64;
    }
    bool addChord(int step, int root, const std::vector<std::int16_t>& intervals) override {
        chord_step = step;
        chord_root = root;
        chord_intervals = intervals;
        return step >= 0 && step < 64;
    }
    bool removeNote(int step) override {
        removed_step = step;
        return step == added_step;
    }
    bool setTempo(double bpm) override {
        tempo = bpm;
        return true;
    }
    void togglePlayback() override { ++play_toggles; }
    void addTrack() override { ++tracks_added; }
    bool selectInstrument(int index) override {
        instrument = index;
        return index >= 0;
    }
    bool transposeSelection(int semitones) override {
        transposed = semitones;
        return true;
    }
    bool clearPattern() override {
        cleared = true;
        return true;
    }
};

void test_action_dispatcher() {
    EventBus bus;
    ActionDispatcher dispatcher(bus);
    FakeSink sink;
    int guide_events = 0;
    bus.subscribe([&](const AppEvent& e) {
        if (e.source == EventSource::guide) ++guide_events;
    });

    // add-note through the sink, announced as the guide's doing.
    auto r = dispatcher.dispatch("add-note", {{"step", "4"}, {"key", "64"}}, sink);
    assert(r.ok && !r.needs_confirm);
    assert(sink.added_step == 4 && sink.added_key == 64);
    assert(guide_events == 1);
    assert(r.message.find("E4") != std::string::npos);

    // Out-of-range step: the sink refuses, no event, honest message.
    r = dispatcher.dispatch("add-note", {{"step", "99"}, {"key", "60"}}, sink);
    assert(!r.ok && guide_events == 1);

    // add-chord: intervals parsed, chord named in the message.
    r = dispatcher.dispatch("add-chord",
                            {{"step", "0"}, {"root", "60"}, {"intervals", "0,4,7"}}, sink);
    assert(r.ok);
    assert(sink.chord_step == 0 && sink.chord_root == 60);
    assert(sink.chord_intervals.size() == 3 && sink.chord_intervals[2] == 7);
    assert(r.message.find("C4") != std::string::npos &&
           r.message.find("G4") != std::string::npos);

    // Tempo clamping.
    r = dispatcher.dispatch("set-tempo", {{"bpm", "500"}}, sink);
    assert(!r.ok && sink.tempo == 0);
    r = dispatcher.dispatch("set-tempo", {{"bpm", "128"}}, sink);
    assert(r.ok && sink.tempo == 128);

    // Destructive: first call asks, second (confirmed) acts.
    r = dispatcher.dispatch("clear-pattern", {}, sink);
    assert(!r.ok && r.needs_confirm && !sink.cleared);
    assert(dispatcher.is_destructive("clear-pattern"));
    assert(!dispatcher.is_destructive("add-note"));
    r = dispatcher.dispatch("clear-pattern", {}, sink, true);
    assert(r.ok && sink.cleared && guide_events == 4);

    // Unknown action: honest, no crash.
    r = dispatcher.dispatch("levitate", {}, sink);
    assert(!r.ok && !r.needs_confirm);
    // A key off the keyboard is refused before it reaches the pattern.
    sink.added_step = -1;
    r = dispatcher.dispatch("add-note", {{"step", "2"}, {"key", "200"}}, sink);
    assert(!r.ok && sink.added_step == -1 && r.message.find("0 to 127") != std::string::npos);
    r = dispatcher.dispatch("add-chord", {{"step", "2"}, {"root", "125"}, {"intervals", "0,4,7"}}, sink);
    assert(!r.ok && r.message.find("0 to 127") != std::string::npos);
    std::cout << "action_dispatcher ok\n";
}

const char* kLessonJson = R"({
  "id": "first-beat",
  "title": "Your first beat",
  "steps": [
    {"title": "STEPS", "text": "Click a step.", "spotlight": "stepGrid", "check": "none"},
    {"title": "KICK", "text": "Add three kicks \u2014 like this.", "spotlight": "stepGrid",
     "action": {"id": "add-note", "args": {"step": "0", "key": "36"}},
     "check": "note-added", "count": 3},
    {"title": "PLAY", "text": "Press play.", "spotlight": "playButton", "check": "transport-started"}
  ]
})";

void test_lesson_engine() {
    LessonEngine engine;
    std::string error;
    assert(engine.load(kLessonJson, &error));
    assert(error.empty());
    assert(engine.has_lesson());
    assert(engine.lesson().id == "first-beat");
    assert(engine.lesson().steps.size() == 3);

    engine.start();
    assert(engine.step_index() == 0);
    assert(engine.step().spotlight == "stepGrid");
    // "none" checks never auto-advance.
    assert(!engine.check_event(AppEvent{EventSource::user, EventKind::note_added, "", ""}));
    assert(engine.step_index() == 0);
    engine.next();
    assert(engine.step_index() == 1);
    assert(engine.step().has_action);
    assert(engine.step().action.id == "add-note");
    assert(engine.step().action.args.at("key") == "36");
    // The right event advances; the wrong one doesn't.
    assert(!engine.check_event(AppEvent{EventSource::user, EventKind::tempo_changed, "", ""}));
    // The guide's own note and an agent's do not count toward the producer's
    // three; the producer's do, one at a time.
    assert(!engine.check_event(AppEvent{EventSource::guide, EventKind::note_added, "", ""}));
    assert(!engine.check_event(AppEvent{EventSource::mcp, EventKind::note_added, "", ""}));
    assert(engine.check_event(AppEvent{EventSource::user, EventKind::note_added, "", ""}));
    assert(engine.check_event(AppEvent{EventSource::user, EventKind::note_added, "", ""}));
    assert(engine.step_index() == 1 && engine.progress() == 2);
    assert(engine.check_event(AppEvent{EventSource::user, EventKind::note_added, "", ""}));
    assert(engine.step_index() == 2 && engine.progress() == 0);
    // \u escapes, as any JSON writer makes them, come through as UTF-8.
    engine.back();
    assert(engine.step().text == "Add three kicks \xE2\x80\x94 like this.");
    engine.next();
    assert(engine.at_end() == false);
    engine.next();
    assert(engine.at_end());
    engine.back();
    assert(engine.step_index() == 2);

    // Malformed JSON: false + error, no crash.
    LessonEngine bad;
    assert(!bad.load("{not json", &error));
    assert(!error.empty() && !bad.has_lesson());
    LessonEngine empty;
    assert(!empty.load(R"({"id": "x", "title": "y", "steps": []})", &error));
    // A hostile file is refused, never a crash.
    LessonEngine deep;
    assert(!deep.load(std::string(100000, '[') + std::string(100000, ']'), &error));
    assert(!deep.load(R"({"id":"x","steps":[{"text":"t","count":0}]})", &error));
    assert(!deep.load(R"({"id":"x","steps":[{"text":"t","action":{"id":"a","args":{"k":1e300}}}]})", &error));
    assert(!deep.load(R"({"id":"x","steps":[{"text":"\ud800"}]})", &error));
    std::cout << "lesson_engine ok\n";
}

void test_llm_client() {
    LlmClient client;
    // Disabled by default: no endpoint, no key.
    assert(!client.enabled());
    bool errored = false;
    client.ask("why?", "",
               [&](const std::string&) { assert(false); },
               [&](const std::string&) { errored = true; });
    assert(errored);

    client.configure("https://api.example.com/v1", "sekret", "test-model");
    assert(client.enabled());
    const auto body = client.build_request("why is the sky blue?", "120 BPM, C major");
    assert(body.find("\"model\":\"test-model\"") != std::string::npos);
    assert(body.find("why is the sky blue?") != std::string::npos);
    assert(body.find("120 BPM, C major") != std::string::npos);
    assert(body.find("sekret") == std::string::npos); // key goes in the header, never the body

    // Response parsing.
    assert(LlmClient::parse_answer(
               R"({"choices":[{"message":{"content":"Because thirds."}}]})") == "Because thirds.");
    assert(LlmClient::parse_answer(R"({"choices":[]})").empty());

    // Fake transport: success and failure paths, no network.
    client.set_transport([](const std::string& request,
                            std::function<void(const std::string&, const std::string&)> done) {
        assert(request.find("/chat/completions") == std::string::npos); // body only; URL is the transport's job
        done(R"({"choices":[{"message":{"content":"Fake answer."}}]})", "");
    });
    std::string answer;
    errored = false;
    client.ask("q?", "", [&](const std::string& a) { answer = a; },
               [&](const std::string&) { errored = true; });
    assert(!errored && answer == "Fake answer.");

    client.set_transport([](const std::string&,
                            std::function<void(const std::string&, const std::string&)> done) {
        done("", "connection refused");
    });
    answer.clear();
    errored = false;
    client.ask("q?", "", [&](const std::string& a) { answer = a; },
               [&](const std::string& e) { errored = !e.empty(); });
    assert(answer.empty() && errored);

    // Every control character is escaped, so the body stays JSON.
    const std::string escaped = client.build_request(std::string("a\x01" "b\x1f"), "");
    assert(escaped.find("a\\u0001b\\u001f") != std::string::npos);
    // A \u escape in the reply, a surrogate pair included, is read as UTF-8.
    assert(LlmClient::parse_answer(R"({"choices":[{"message":{"content":"a \u2014 b \ud83c\udfb5"}}]})") ==
           "a \xE2\x80\x94 b \xF0\x9F\x8E\xB5");
    std::cout << "llm_client ok\n";
}

} // namespace

int main() {
    test_event_bus();
    test_narrator();
    test_action_dispatcher();
    test_lesson_engine();
    test_llm_client();
    std::cout << "guide_tests: all pass\n";
    return 0;
}
