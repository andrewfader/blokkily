#pragma once

// The wire format between Blokkily and an LLM backend. The model answers in
// this JSON dialect; these functions are the only place that reads or writes
// it, so the prompt, the parser and the tests cannot drift apart.
//
// Steps are sixteenth notes (PatternModel::ticks_per_step ticks), which is
// how every editor counts, so "put a note on step 4" means the same thing to
// the model, the producer and the grid.

#include "blokkily/model/event.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace blokkily::llm {

// What a response asks for: replace the pattern, add to it, or hand back a
// modified copy of what it was shown (which is applied as a replace).
enum class Mode { replace, add, modify };

struct Response {
    Mode mode = Mode::replace;
    std::vector<Trigger> triggers;
};

struct ParseResult {
    bool ok = false;
    QString error;             // why `ok` is false, in a sentence a producer can read
    Response response;
    QStringList corrections;   // clamped or repaired fields, for honesty in the UI
};

// The open pattern as the model is shown it, in the same dialect it answers
// in, so "make this jazzier" can be read against what is actually there.
[[nodiscard]] QJsonArray triggersToJson(std::span<const Trigger> triggers, Tick ticks_per_step);

// One trigger object of the response dialect. ticks_per_step converts steps
// to ticks; key_limit is the tuning's degree count, beyond which a key is
// out of the instrument's world.
[[nodiscard]] std::optional<Trigger> triggerFromJson(const QJsonObject& object,
                                                     Tick ticks_per_step, int key_limit,
                                                     QString* error);

// Parses and validates a model's reply. Never throws, never aborts: a bad
// reply is an error string, not a crash, because models sometimes answer
// prose despite being told not to.
[[nodiscard]] ParseResult parseResponse(const QString& text, Tick ticks_per_step,
                                        int pattern_steps, int key_limit);

// Extracts the JSON object from a reply that may have fenced it in markdown
// or padded it with commentary.
[[nodiscard]] std::optional<QJsonObject> extractJsonObject(const QString& text,
                                                           QString* error);

} // namespace blokkily::llm
