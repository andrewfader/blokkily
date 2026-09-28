#pragma once

// Header for the OpenRouter backend so the unit test can see its full
// type (manage lifetime, swap network access manager). Production code
// uses the LlmBackend interface through makeOpenRouterBackend().

#include "openai_compatible_backend.hpp"

namespace blokkily::llm {

class OpenRouterBackend final : public OpenAiCompatibleBackend {
public:
    explicit OpenRouterBackend(QObject* parent = nullptr);

    // Inherited from OpenAiCompatibleBackend:
    //   void complete(const Request&, Completion) override;
    //   QString displayName() const override;
    //   void setNetworkForTesting(QNetworkAccessManager*);
};

[[nodiscard]] OpenRouterBackend* makeOpenRouterBackendForTesting(QObject* parent = nullptr);
void setOpenRouterBackendNetworkForTesting(OpenRouterBackend* backend,
                                           QNetworkAccessManager* nam);

} // namespace blokkily::llm
