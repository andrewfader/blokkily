// The MiniMax / Minimax backend — a thin shell over the shared
// OpenAI-compatible base. The full request / reply / error plumbing lives
// in openai_compatible_backend.cpp; this file only describes who MiniMax
// is and which env vars to read.
//
// The key comes from BLOKKILY_MINIMAX_KEY; without one the backend stays
// selectable and reports why it cannot answer (matching the Gemini shape —
// AGENTS.md / docs/llm-assistant.md: backends must not silently disappear
// from the picker). The class is declared in minimax_backend.hpp so the
// unit test can stand up a backend, swap in a capturing
// QNetworkAccessManager, and exercise the whole request / reply / error
// path without a live server (AGENTS.md: tests must not depend on a
// network connection).

#include "minimax_backend.hpp"

#include "openai_compatible_backend.hpp"

namespace blokkily::llm {

namespace {
OpenAiCompatibleBackend::Config minimaxConfig() {
    return OpenAiCompatibleBackend::Config{
        QStringLiteral("MiniMax"),
        QStringLiteral("BLOKKILY_MINIMAX_KEY"),
        QStringLiteral("BLOKKILY_MINIMAX_URL"),
        QStringLiteral("https://api.minimax.io"),
        QStringLiteral("BLOKKILY_MINIMAX_MODEL"),
        QStringLiteral("MiniMax-M3"),
        QStringLiteral("/v1/chat/completions"),
    };
}
} // namespace

MinimaxBackend::MinimaxBackend(QObject* parent)
    : OpenAiCompatibleBackend(minimaxConfig(), parent) {}

std::unique_ptr<LlmBackend> makeMinimaxBackend(QObject* parent) {
    return std::make_unique<MinimaxBackend>(parent);
}

MinimaxBackend* makeMinimaxBackendForTesting(QObject* parent) {
    return new MinimaxBackend(parent);
}

void setMinimaxBackendNetworkForTesting(MinimaxBackend* backend,
                                        QNetworkAccessManager* nam) {
    setOpenAiCompatibleBackendNetworkForTesting(backend, nam);
}

} // namespace blokkily::llm
