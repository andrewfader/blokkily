#pragma once

// Shared base for backends that speak the OpenAI chat-completions dialect —
// MiniMax / Minimax, ChatGPT, OpenRouter, and any future vendor that adopts
// the same JSON shape (Authorization: Bearer, POST /v1/chat/completions,
// {"choices":[{"message":{"content":...}}]}, response_format json_object).
//
// The wire dialect the assistant already parses (mode + triggers) is
// unchanged: same input format, same output format, same parsing path. The
// subclasses only say who they are and where to find them. The class lives
// here so the unit tests can stand up a backend, swap in a capturing
// QNetworkAccessManager, and exercise the whole request / reply / error
// path without a live server (AGENTS.md: tests must not depend on a
// network connection).

#include "llm_backend.hpp"

#include <QNetworkAccessManager>
#include <QString>

namespace blokkily::llm {

class OpenAiCompatibleBackend : public LlmBackend {
public:
    // Subclasses describe themselves: a short label the picker shows, the
    // env vars they read, and the defaults they fall back to.
    struct Config {
        QString label;        // "MiniMax" / "ChatGPT" / "OpenRouter" — appears in error text
        QString keyEnvVar;    // "BLOKKILY_MINIMAX_KEY" etc.
        QString urlEnvVar;    // optional — empty means no override
        QString defaultUrl;
        QString modelEnvVar;  // optional
        QString defaultModel;
        QString chatPath;     // usually /v1/chat/completions
    };

    OpenAiCompatibleBackend(Config config, QObject* parent = nullptr);

    void complete(const Request& request, Completion completion) override;
    [[nodiscard]] QString displayName() const override;

    // Test seam only — replaces the backend's QNetworkAccessManager so a
    // test can intercept the POST without a live server (AGENTS.md).
    void setNetworkForTesting(QNetworkAccessManager* nam);

private:
    Config config_;
    QNetworkAccessManager* network_;
    QString base_;     // URL with trailing slashes trimmed
    QString model_;
    QString key_;
};

// Free factory helpers — same shape as makeMinimaxBackendForTesting so the
// test harness can grab a concrete subclass pointer when it needs to call
// setNetworkForTesting through the public free function.
[[nodiscard]] OpenAiCompatibleBackend* makeOpenAiCompatibleBackendForTesting(
    OpenAiCompatibleBackend::Config config, QObject* parent = nullptr);
void setOpenAiCompatibleBackendNetworkForTesting(OpenAiCompatibleBackend* backend,
                                                 QNetworkAccessManager* nam);

} // namespace blokkily::llm
