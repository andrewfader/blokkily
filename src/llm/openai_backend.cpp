// The ChatGPT backend — a thin shell over the shared OpenAI-compatible
// base. Hits https://api.openai.com/v1/chat/completions with a Bearer key
// (the same shape as every other OpenAI-compat vendor) and reads
// choices[0].message.content.
//
// The key comes from BLOKKILY_OPENAI_KEY; without one the backend stays
// selectable and reports why it cannot answer, matching the Gemini /
// MiniMax shape — AGENTS.md / docs/llm-assistant.md: backends must not
// silently disappear from the picker. The class is declared in
// openai_backend.hpp so the unit test can stand up a backend, swap in a
// capturing QNetworkAccessManager, and exercise the whole request /
// reply / error path without a live server (AGENTS.md: tests must not
// depend on a network connection).

#include "openai_backend.hpp"

#include "openai_compatible_backend.hpp"

namespace blokkily::llm {

namespace {
OpenAiCompatibleBackend::Config openaiConfig() {
    return OpenAiCompatibleBackend::Config{
        QStringLiteral("ChatGPT"),
        QStringLiteral("BLOKKILY_OPENAI_KEY"),
        QStringLiteral("BLOKKILY_OPENAI_URL"),
        QStringLiteral("https://api.openai.com"),
        QStringLiteral("BLOKKILY_OPENAI_MODEL"),
        QStringLiteral("gpt-4o-mini"),
        QStringLiteral("/v1/chat/completions"),
    };
}
} // namespace

OpenAiBackend::OpenAiBackend(QObject* parent)
    : OpenAiCompatibleBackend(openaiConfig(), parent) {}

std::unique_ptr<LlmBackend> makeOpenAiBackend(QObject* parent) {
    return std::make_unique<OpenAiBackend>(parent);
}

OpenAiBackend* makeOpenAiBackendForTesting(QObject* parent) {
    return new OpenAiBackend(parent);
}

void setOpenAiBackendNetworkForTesting(OpenAiBackend* backend,
                                       QNetworkAccessManager* nam) {
    setOpenAiCompatibleBackendNetworkForTesting(backend, nam);
}

} // namespace blokkily::llm
