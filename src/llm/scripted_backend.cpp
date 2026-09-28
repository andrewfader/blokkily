#include "scripted_backend.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace blokkily::llm {

ScriptedBackend::ScriptedBackend(QObject* parent) : LlmBackend(parent) {
    const auto path = qEnvironmentVariable("BLOKKILY_LLM_SCRIPT");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!file.atEnd()) {
        const auto line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        const auto object = QJsonDocument::fromJson(line).object();
        lines_.push_back({object.value("match").toString(),
                          object.value("reply").toString()});
    }
    loaded_ = true;
}

void ScriptedBackend::complete(const Request& request, Completion completion) {
    last_ = request;
    Reply answer;
    if (!loaded_) {
        answer.error = QStringLiteral("No script: set BLOKKILY_LLM_SCRIPT to a file of "
                                      "{\"match\", \"reply\"} lines.");
    } else {
        for (const auto& line : lines_) {
            if (line.match.isEmpty() || request.prompt.contains(line.match)) {
                answer.ok = true;
                answer.text = line.reply;
                break;
            }
        }
        if (!answer.ok)
            answer.error = QStringLiteral("The script has no line matching this prompt.");
    }
    // Answered from the event loop, the way a network answer arrives, so the
    // gate drives the same asynchronous path as a live backend.
    QTimer::singleShot(0, this, [completion = std::move(completion), answer] {
        completion(answer);
    });
}

QString ScriptedBackend::displayName() const {
    return QStringLiteral("Scripted");
}

} // namespace blokkily::llm
