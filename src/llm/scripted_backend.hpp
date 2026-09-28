#pragma once

// The deterministic backend: no server, no key, no network. It answers from
// a script so the BDD gate and the unit tests can drive the whole assistant
// path — prompt, parse, preview, apply, undo — exactly as a live backend
// would, and get the same bytes every run (AGENTS.md: tests must not depend
// on a network connection).
//
// The script comes from the file BLOKKILY_LLM_SCRIPT names: one JSON object
// per line, {"match": "substring of the prompt", "reply": "the model's
// answer"}. The first line whose match the prompt contains wins; with no
// match the backend answers with an error, the way a live one that failed
// would. An empty match matches everything.

#include "llm_backend.hpp"

namespace blokkily::llm {

class ScriptedBackend final : public LlmBackend {
    Q_OBJECT
public:
    explicit ScriptedBackend(QObject* parent = nullptr);

    void complete(const Request& request, Completion completion) override;
    [[nodiscard]] QString displayName() const override;

    // What the last request carried, so a test can check the context the
    // model would have seen.
    [[nodiscard]] const Request& lastRequest() const noexcept { return last_; }

private:
    struct Line {
        QString match;
        QString reply;
    };
    std::vector<Line> lines_;
    Request last_;
    bool loaded_ = false;
};

} // namespace blokkily::llm
