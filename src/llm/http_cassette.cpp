// The cassette network: records a backend's real exchanges with its provider
// to JSONL, or answers them back from that file with no network at all.
// http_cassette.hpp has the file format and the matching rules.

#include "http_cassette.hpp"

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

namespace blokkily::llm {
namespace {

const QByteArray redacted = QByteArrayLiteral("<redacted>");

// An answer whose bytes the cassette supplies — from the provider while
// recording, from the file while replaying. Finishes on the event loop, the
// way a real reply does.
class CassetteReply final : public QNetworkReply {
public:
    CassetteReply(QObject* parent, const QNetworkRequest& request,
                  QNetworkAccessManager::Operation op)
        : QNetworkReply(parent) {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        open(QIODevice::ReadOnly);
    }

    void deliver(int status, NetworkError error, const QString& errorString,
                 QByteArray body) {
        payload_ = std::move(body);
        if (status > 0) setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (error != NoError) setError(error, errorString);
        QMetaObject::invokeMethod(
            this,
            [this, error] {
                if (error != NoError) emit errorOccurred(error);
                setFinished(true);
                emit readyRead();
                emit finished();
            },
            Qt::QueuedConnection);
    }

    void abort() override {}
    qint64 bytesAvailable() const override {
        return payload_.size() - consumed_ + QIODevice::bytesAvailable();
    }
    bool isSequential() const override { return true; }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const qint64 give =
            std::min<qint64>(maxlen, static_cast<qint64>(payload_.size()) - consumed_);
        if (give <= 0) return isFinished() ? -1 : 0;
        std::memcpy(data, payload_.constData() + consumed_, static_cast<std::size_t>(give));
        consumed_ += give;
        return give;
    }

private:
    QByteArray payload_;
    qint64 consumed_ = 0;
};

const char* methodName(QNetworkAccessManager::Operation op) {
    switch (op) {
    case QNetworkAccessManager::GetOperation: return "GET";
    case QNetworkAccessManager::PostOperation: return "POST";
    case QNetworkAccessManager::PutOperation: return "PUT";
    case QNetworkAccessManager::DeleteOperation: return "DELETE";
    case QNetworkAccessManager::HeadOperation: return "HEAD";
    default: return "CUSTOM";
    }
}

// JSON bodies are compared and stored as JSON, so key order or whitespace
// never makes two equal requests differ, and the cassette reads as JSON.
QByteArray normalized(const QByteArray& body) {
    const auto document = QJsonDocument::fromJson(body);
    return document.isNull() ? body : document.toJson(QJsonDocument::Compact);
}

QJsonValue bodyValue(const QByteArray& body) {
    const auto document = QJsonDocument::fromJson(body);
    if (document.isObject()) return document.object();
    if (document.isArray()) return document.array();
    return QString::fromUtf8(body);
}

QByteArray bodyBytes(const QJsonValue& value) {
    if (value.isObject()) return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray()) return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    return value.toString().toUtf8();
}

// Every credential the request carries, so none of them reaches the file.
std::vector<QByteArray> secretsOf(const QNetworkRequest& request) {
    std::vector<QByteArray> secrets;
    auto authorization = request.rawHeader("Authorization");
    if (authorization.startsWith("Bearer ")) authorization = authorization.mid(7);
    secrets.push_back(authorization.trimmed());
    secrets.push_back(request.rawHeader("x-goog-api-key").trimmed());
    secrets.push_back(request.rawHeader("x-api-key").trimmed());
    secrets.push_back(QUrlQuery(request.url()).queryItemValue(QStringLiteral("key")).toUtf8());
    std::erase_if(secrets, [](const QByteArray& secret) { return secret.isEmpty(); });
    return secrets;
}

QString scrubbedUrl(const QUrl& url) {
    QUrlQuery query(url);
    if (!query.hasQueryItem(QStringLiteral("key"))) return url.toString();
    query.removeAllQueryItems(QStringLiteral("key"));
    query.addQueryItem(QStringLiteral("key"), QString::fromLatin1(redacted));
    QUrl copy(url);
    copy.setQuery(query);
    return copy.toString();
}

} // namespace

CassetteNetworkAccessManager::CassetteNetworkAccessManager(Mode mode, QString path,
                                                           QObject* parent,
                                                           QNetworkAccessManager* upstream)
    : QNetworkAccessManager(parent), mode_(mode), path_(std::move(path)),
      upstream_(upstream) {
    if (mode_ == Mode::replay) load();
    if (mode_ == Mode::record && upstream_ == nullptr)
        upstream_ = new QNetworkAccessManager(this);
    if (upstream_ != nullptr) upstream_->setParent(this);
}

void CassetteNetworkAccessManager::load() {
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) return;
    while (!file.atEnd()) {
        const auto line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        const auto exchange = QJsonDocument::fromJson(line).object();
        const auto request = exchange.value("request").toObject();
        tape_.push_back({request.value("method").toString().toUtf8(),
                         request.value("url").toString(),
                         normalized(bodyBytes(request.value("body"))),
                         exchange.value("response").toObject()});
    }
}

void CassetteNetworkAccessManager::append(const QJsonObject& line,
                                          const std::vector<QByteArray>& secrets) const {
    auto bytes = QJsonDocument(line).toJson(QJsonDocument::Compact);
    for (const auto& secret : secrets) bytes.replace(secret, redacted);
    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        qWarning("cassette: cannot write %s", qPrintable(path_));
        return;
    }
    file.write(bytes + '\n');
}

QNetworkReply* CassetteNetworkAccessManager::createRequest(Operation op,
                                                           const QNetworkRequest& request,
                                                           QIODevice* outgoing) {
    const QByteArray body = outgoing != nullptr ? outgoing->readAll() : QByteArray();
    const QByteArray method = methodName(op);
    const QString url = scrubbedUrl(request.url());
    auto* reply = new CassetteReply(this, request, op);

    if (mode_ == Mode::replay) {
        const auto key = normalized(body);
        int sameUrl = 0;
        for (auto& exchange : tape_) {
            if (exchange.used || exchange.method != method || exchange.url != url) continue;
            ++sameUrl;
            if (exchange.body != key) continue;
            exchange.used = true;
            const auto& response = exchange.response;
            reply->deliver(response.value("status").toInt(),
                           static_cast<QNetworkReply::NetworkError>(
                               response.value("error").toInt()),
                           response.value("error_string").toString(),
                           bodyBytes(response.value("body")));
            return reply;
        }
        reply->deliver(0, QNetworkReply::ContentNotFoundError,
                       QStringLiteral("the cassette %1 has no recorded answer for %2 %3 "
                                      "(%4 unused for that URL, none with this body); "
                                      "record it again")
                           .arg(path_, QString::fromLatin1(method), url)
                           .arg(sameUrl),
                       {});
        return reply;
    }

    // Recording: the provider answers through the upstream network, and
    // the exchange is kept. The hand-over dies with our reply.
    auto* sent = new QBuffer;
    sent->setData(body);
    sent->open(QIODevice::ReadOnly);
    auto* live = op == PostOperation ? upstream_->post(request, sent)
                                     : upstream_->sendCustomRequest(request, method, sent);
    sent->setParent(live);
    connect(reply, &QObject::destroyed, live, &QObject::deleteLater);
    connect(live, &QNetworkReply::finished, reply,
            [this, live, reply, method, url, body, secrets = secretsOf(request)] {
                live->deleteLater();
                const auto answer = live->readAll();
                const int status =
                    live->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QJsonObject line{
                    {"request",
                     QJsonObject{{"method", QString::fromLatin1(method)},
                                 {"url", url},
                                 {"body", bodyValue(body)}}},
                    {"response",
                     QJsonObject{{"status", status},
                                 {"error", static_cast<int>(live->error())},
                                 {"error_string", live->error() == QNetworkReply::NoError
                                                      ? QString()
                                                      : live->errorString()},
                                 {"body", bodyValue(answer)}}},
                };
                append(line, secrets);
                reply->deliver(status, live->error(), live->errorString(), answer);
            });
    return reply;
}

QNetworkAccessManager* cassetteFromEnvironment(QObject* parent) {
    const auto path = qEnvironmentVariable("BLOKKILY_LLM_CASSETTE");
    if (path.isEmpty()) return nullptr;
    const auto mode = qEnvironmentVariable("BLOKKILY_LLM_CASSETTE_MODE") == QLatin1String("record")
                          ? CassetteNetworkAccessManager::Mode::record
                          : CassetteNetworkAccessManager::Mode::replay;
    return new CassetteNetworkAccessManager(mode, path, parent);
}

} // namespace blokkily::llm
