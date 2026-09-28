// The OpenRouter backend — a thin shell over the shared OpenAI-compatible
// base. OpenRouter exposes the same /v1/chat/completions dialect that
// ChatGPT does; the only difference is the host
// (https://openrouter.ai/api) and the model id (an OpenRouter slug like
// "openai/gpt-4o-mini" or "anthropic/claude-3.5-sonnet"). Routing
// happens on the server side; the wire format is unchanged.
//
// The key comes from BLOKKILY_OPENROUTER_KEY; without one the backend
// stays selectable and reports why it cannot answer, matching the other
// cloud backends — AGENTS.md: backends must not silently disappear from
// the picker. The class is declared in openrouter_backend.hpp so the
// unit test can stand up a backend, swap in a capturing
// QNetworkAccessManager, and exercise the whole request / reply / error
// path without a live server (AGENTS.md: tests must not depend on a
// network connection).

#include "openrouter_backend.hpp"

#include "openai_compatible_backend.hpp"

namespace blokkily::llm {

namespace {
OpenAiCompatibleBackend::Config openrouterConfig() {
    return OpenAiCompatibleBackend::Config{
        QStringLiteral("OpenRouter"),
        QStringLiteral("BLOKKILY_OPENROUTER_KEY"),
        QStringLiteral("BLOKKILY_OPENROUTER_URL"),
        QStringLiteral("https://openrouter.ai/api"),
        QStringLiteral("BLOKKILY_OPENROUTER_MODEL"),
        QStringLiteral("openai/gpt-4o-mini"),
        QStringLiteral("/v1/chat/completions"),
    };
}
} // namespace

OpenRouterBackend::OpenRouterBackend(QObject* parent)
    : OpenAiCompatibleBackend(openrouterConfig(), parent) {}

std::unique_ptr<LlmBackend> makeOpenRouterBackend(QObject* parent) {
    return std::make_unique<OpenRouterBackend>(parent);
}

OpenRouterBackend* makeOpenRouterBackendForTesting(QObject* parent) {
    return new OpenRouterBackend(parent);
}

void setOpenRouterBackendNetworkForTesting(OpenRouterBackend* backend,
                                           QNetworkAccessManager* nam) {
    setOpenAiCompatibleBackendNetworkForTesting(backend, nam);
}

} // namespace blokkily::llm
