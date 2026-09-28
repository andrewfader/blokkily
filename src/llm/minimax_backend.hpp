#pragma once

// Header for the MiniMax backend so the unit test can see its full type
// (manage lifetime, swap network access manager). Production code uses
// the LlmBackend interface through makeMinimaxBackend(). The MiniMax
// backend inherits the OpenAI-compatible plumbing from
// OpenAiCompatibleBackend.

#include "openai_compatible_backend.hpp"

namespace blokkily::llm {

class MinimaxBackend final : public OpenAiCompatibleBackend {
public:
    explicit MinimaxBackend(QObject* parent = nullptr);

    // Inherited from OpenAiCompatibleBackend:
    //   void complete(const Request&, Completion) override;
    //   QString displayName() const override;
    //   void setNetworkForTesting(QNetworkAccessManager*);
};

[[nodiscard]] MinimaxBackend* makeMinimaxBackendForTesting(QObject* parent = nullptr);
void setMinimaxBackendNetworkForTesting(MinimaxBackend* backend,
                                        QNetworkAccessManager* nam);

} // namespace blokkily::llm
