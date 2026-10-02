#pragma once

// VCR-style HTTP recording for the live backends. A cassette is a JSONL file
// of real exchanges with a provider: each line is one request (method, URL,
// body) and the answer it got (HTTP status, Qt's transport error, body).
//
//   record — every request goes to the real provider; the exchange is
//            appended to the cassette with the key scrubbed out of it.
//   replay — no network: each request is answered from the first unused
//            exchange whose method, URL and body match it exactly. A request
//            the cassette never saw fails readably, naming the cassette, so
//            a changed prompt or context shows up as a test failure rather
//            than a silent pass.
//
// Recording proves the backend works against the provider today; replay
// keeps that proof in the suite without a key, a network, or a model's
// mood (AGENTS.md: tests must not depend on a network connection).
//
// Credentials never reach the file: request headers are not recorded at all,
// and every credential the request carried (Bearer token, x-goog-api-key)
// is replaced with <redacted> anywhere it appears in the line, including a
// provider echoing it back in an error.

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>

#include <vector>

namespace blokkily::llm {

class CassetteNetworkAccessManager final : public QNetworkAccessManager {
    Q_OBJECT
public:
    enum class Mode { record, replay };

    // While recording, `upstream` carries each request to the provider: the
    // live network when none is given, a stand-in when a test supplies one.
    // The cassette takes ownership.
    CassetteNetworkAccessManager(Mode mode, QString path, QObject* parent = nullptr,
                                 QNetworkAccessManager* upstream = nullptr);

    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] const QString& path() const noexcept { return path_; }

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoing) override;

private:
    struct Exchange {
        QByteArray method;
        QString url;
        QByteArray body;   // compact JSON when the body was JSON
        QJsonObject response;
        bool used = false;
    };
    void load();
    void append(const QJsonObject& line, const std::vector<QByteArray>& secrets) const;

    Mode mode_;
    QString path_;
    QNetworkAccessManager* upstream_ = nullptr;
    std::vector<Exchange> tape_;
};

// The cassette the environment asks for, or nullptr for the real network:
//   BLOKKILY_LLM_CASSETTE       the JSONL file
//   BLOKKILY_LLM_CASSETTE_MODE  "record" to call the provider and append,
//                               anything else (default) to replay
[[nodiscard]] QNetworkAccessManager* cassetteFromEnvironment(QObject* parent);

} // namespace blokkily::llm
