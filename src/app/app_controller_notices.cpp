// What the window tells the producer (features/notices.feature): failures
// that used to land in a status line at the foot of a bar that hides, and
// work that finished, shown over the editors where they are seen.
#include "app_controller.hpp"

#include <algorithm>

namespace {
// More than this and the oldest goes: a burst of failures stays readable.
constexpr std::size_t most_notices = 4;
} // namespace

QVariantList AppController::notices() const {
    QVariantList list;
    for (const auto& notice : notices_)
        list.push_back(QVariantMap{{"id", notice.id}, {"level", notice.level}, {"text", notice.text}});
    return list;
}

void AppController::notify(const QString& level, const QString& text) {
    if (text.isEmpty()) return;
    std::erase_if(notices_, [&](const Notice& notice) { return notice.text == text; });
    notices_.push_back({next_notice_++, level, text});
    while (notices_.size() > most_notices) notices_.erase(notices_.begin());
    emit noticesChanged();
}

void AppController::dismissNotice(int id) {
    if (std::erase_if(notices_, [id](const Notice& notice) { return notice.id == id; }) > 0)
        emit noticesChanged();
}
