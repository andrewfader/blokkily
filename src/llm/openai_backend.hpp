#pragma once

// Header for the ChatGPT backend so the unit test can see its full type
// (manage lifetime, swap network access manager). Production code uses
// the LlmBackend interface through makeOpenAiBackend().

#include "openai_compatible_backend.hpp"

namespace blokkily::llm {

class OpenAiBackend final : public OpenAiCompatibleBackend {
public:
    explicit OpenAiBackend(QObject* parent = nullptr);

    // Inherited from OpenAiCompatibleBackend:
    //   void complete(const Request&, Completion) override;
    //   QString displayName() const override;
    //   void setNetworkForTesting(QNetworkAccessManager*);
};

[[nodiscard]] OpenAiBackend* makeOpenAiBackendForTesting(QObject* parent = nullptr);
void setOpenAiBackendNetworkForTesting(OpenAiBackend* backend,
                                       QNetworkAccessManager* nam);

} // namespace blokkily::llm
