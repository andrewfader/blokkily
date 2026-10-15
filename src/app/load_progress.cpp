#include "load_progress.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>

#include <cmath>
#include <utility>

LoadProgress::LoadProgress(QObject* parent) : QObject(parent) {
    // A gate can ask for every load to be shown and drawn at every percent
    // before the first one starts.
    if (const auto reveal = qEnvironmentVariable("BLOKKILY_PROGRESS_REVEAL_MS"); !reveal.isEmpty())
        reveal_ms_ = std::max(0, reveal.toInt());
}

LoadProgress::~LoadProgress() {
    if (depth_ > 0 && QCoreApplication::instance() != nullptr)
        QCoreApplication::instance()->removeEventFilter(this);
}

LoadProgress::Scope::Scope(LoadProgress& progress, const QString& title) : progress_(progress) {
    progress_.begin(title);
}

LoadProgress::Scope::~Scope() { progress_.end(); }

void LoadProgress::setTiming(int reveal_ms, int frame_ms) {
    reveal_ms_ = std::max(0, reveal_ms);
    frame_ms_ = std::max(0, frame_ms);
}

void LoadProgress::begin(const QString& title) {
    if (depth_++ > 0) return;
    title_ = title;
    detail_.clear();
    fraction_ = -1.0;
    drawn_fraction_ = -2.0;
    drawn_detail_.clear();
    frames_ = 0;
    since_begin_.start();
    since_frame_.invalidate();
    // Nobody is shown anything yet; agents wait from the start, so a request
    // never lands between the load's first step and its first frame.
    emit holdRequests(true);
}

void LoadProgress::end() {
    if (depth_ == 0 || --depth_ > 0) return;
    const bool was_shown = shown_;
    if (was_shown) QCoreApplication::instance()->removeEventFilter(this);
    shown_ = false;
    title_.clear();
    detail_.clear();
    fraction_ = -1.0;
    if (was_shown) emit changed();
    emit holdRequests(false);
    if (after_.empty()) return;
    // Next turn, so what was owed runs after whoever opened the load has
    // finished with what it loaded.
    QTimer::singleShot(0, this, [this, owed = std::exchange(after_, {})] {
        for (const auto& work : owed) work();
    });
}

void LoadProgress::afterLoad(std::function<void()> work) {
    if (depth_ == 0) {
        QTimer::singleShot(0, this, std::move(work));
        return;
    }
    after_.push_back(std::move(work));
}

void LoadProgress::report(double fraction, const QString& detail) {
    if (depth_ == 0 || pumping_) return;
    fraction_ = fraction < 0.0 ? -1.0 : std::min(1.0, fraction);
    if (!detail.isEmpty()) detail_ = detail;
    if (!shown_) {
        if (since_begin_.elapsed() < reveal_ms_) return;
        shown_ = true;
        QCoreApplication::instance()->installEventFilter(this);
        pump();
        return;
    }
    if (since_frame_.isValid() && since_frame_.elapsed() < frame_ms_) return;
    // A frame is only worth drawing when it shows something new: another
    // percent, another step named, or the bar moving while nothing is
    // counted.
    const bool moved = fraction_ < 0.0 || std::abs(fraction_ - drawn_fraction_) >= 0.01 ||
                       detail_ != drawn_detail_;
    if (moved) pump();
}

void LoadProgress::pump() {
    drawn_fraction_ = fraction_;
    drawn_detail_ = detail_;
    emit changed();
    pumping_ = true;
    // Posted events, timers and the window's own update requests: enough for
    // a frame to be laid out and drawn.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    pumping_ = false;
    ++frames_;
    since_frame_.start();
    emit frameDrawn();
}

bool LoadProgress::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    // Nothing can be played, clicked or typed into a half-loaded song, and
    // the window is not closed under the work.
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::ShortcutOverride:
    case QEvent::Shortcut:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
    case QEvent::TabletMove:
    case QEvent::ContextMenu:
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::Drop:
    case QEvent::Close:
        return true;
    default:
        return QObject::eventFilter(watched, event);
    }
}
