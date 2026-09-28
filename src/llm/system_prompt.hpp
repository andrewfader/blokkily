#pragma once

// The standing instructions every backend answer is shaped by: what Blokkily
// is, the vocabulary producers bring from other DAWs, and the JSON dialect
// the answer must be in. Kept as data-adjacent code rather than a resource so
// the tests can pin its promises (the schema it documents is the schema
// trigger_json.cpp parses).

#include <QString>

namespace blokkily::llm {

// The full system prompt. `pattern_steps` is the open pattern's length in
// sixteenth steps, so the model knows the space it is writing in.
[[nodiscard]] QString systemPrompt(int pattern_steps);

} // namespace blokkily::llm
