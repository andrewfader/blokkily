#pragma once

// Header for the MiniMax backend so the unit test can see its full type
// (manage lifetime, swap network access manager). Production code uses
// the LlmBackend interface through makeMinimaxBackend().

#include "llm_backend.hpp"

#include <QNetworkAccessManager>

namespace blokkily::llm {

class MinimaxBackend final : public LlmBackend {
public:
    explicit MinimaxBackend(QObject* parent = nullptr);

    void complete(const Request& request, Completion completion) override;
    [[nodiscard]] QString displayName() const override;

    // Test seam only — replaces the backend's QNetworkAccessManager so a
    // test can intercept the POST without a live server (AGENTS.md).
    void setNetworkForTesting(QNetworkAccessManager* nam);

private:
    QNetworkAccessManager* network_;
    QString base_;
    QString model_;
    QString key_;
};

[[nodiscard]] MinimaxBackend* makeMinimaxBackendForTesting(QObject* parent = nullptr);
void setMinimaxBackendNetworkForTesting(MinimaxBackend* backend,
                                        QNetworkAccessManager* nam);

} // namespace blokkily::llm
