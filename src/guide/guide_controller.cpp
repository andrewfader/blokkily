#include "guide_controller.hpp"

#include "app_controller.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include "blokkily/guide/narrator.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cstdint>
#include <vector>

namespace {

// The sink the dispatcher's actions run through: the same AppController and
// PatternModel methods the QML buttons call, stamped as the guide's doing so
// the narration feed attributes them correctly.
class ControllerSink : public blokkily::guide::ICommandSink {
public:
    ControllerSink(AppController* controller, PatternModel* pattern)
        : controller_(controller), pattern_(pattern) {}

    bool addNote(int step, int key) override {
        if (step < 0 || step >= pattern_->stepCount()) return false;
        stamp();
        pattern_->setStepKey(step, key);
        return pattern_->hasStep(step);
    }
    bool addChord(int step, int root, const std::vector<std::int16_t>& intervals) override {
        if (step < 0 || step >= pattern_->stepCount()) return false;
        stamp();
        blokkily::Chord chord;
        chord.root = static_cast<std::int16_t>(root);
        chord.intervals = intervals;
        pattern_->placeChord(step, chord);
        return pattern_->hasStep(step);
    }
    bool removeNote(int step) override {
        const bool had = pattern_->hasStep(step);
        if (!had) return false;
        stamp();
        pattern_->clearStep(step);
        return true;
    }
    bool setTempo(double bpm) override {
        stamp();
        controller_->setTempo(bpm);
        return true;
    }
    void togglePlayback() override {
        stamp();
        controller_->togglePlayback();
    }
    void addTrack() override {
        stamp();
        controller_->addTrack();
    }
    bool selectInstrument(int index) override {
        stamp();
        return controller_->selectInstrument(index);
    }
    bool transposeSelection(int semitones) override {
        stamp();
        pattern_->transposeSelected(semitones);
        return true;
    }
    bool clearPattern() override {
        bool any = false;
        for (int step = 0; step < pattern_->stepCount(); ++step) {
            if (pattern_->hasStep(step)) {
                stamp();
                pattern_->clearStep(step);
                any = true;
            }
        }
        return any;
    }

private:
    void stamp() {
        controller_->setNextEventSource(blokkily::guide::EventSource::guide);
        pattern_->setNextEventSource(blokkily::guide::EventSource::guide);
    }
    AppController* controller_;
    PatternModel* pattern_;
};

QStringList splitMeter(const QString& meter) { return meter.split('/'); }

} // namespace

GuideController::GuideController(AppController* controller, SongModel* song,
                                 PatternModel* pattern, Transport* transport,
                                 QObject* parent)
    : QObject(parent),
      controller_(controller),
      song_(song),
      pattern_(pattern),
      transport_(transport),
      dispatcher_(controller->eventBus()) {
    QSettings settings("Blokkily", "blokkily");
    guide_enabled_ = settings.value("guide/enabled", true).toBool();
    narrate_enabled_ = settings.value("guide/narrate", true).toBool();
    const QString endpoint = settings.value("guide/llmEndpoint").toString();
    const QString key = settings.value("guide/llmKey").toString();
    endpoint_ = endpoint;
    api_key_ = key;
    if (!endpoint.isEmpty() && !key.isEmpty())
        llm_.configure(endpoint.toStdString(), key.toStdString());

    // The network transport: OpenAI-compatible POST with a short timeout.
    // The key travels in the Authorization header, never in the body or a URL.
    llm_.set_transport([this](const std::string& body,
                              std::function<void(const std::string&, const std::string&)> done) {
        auto* nam = new QNetworkAccessManager(this);
        QNetworkRequest request(QUrl(endpoint_ + "/chat/completions"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", ("Bearer " + api_key_).toUtf8());
        QNetworkReply* reply = nam->post(request, QByteArray::fromStdString(body));
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [reply] { reply->abort(); });
        timer->start(20000);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, timer, nam, done = std::move(done)] {
                    timer->stop();
                    timer->deleteLater();
                    const bool ok = reply->error() == QNetworkReply::NoError;
                    const std::string payload =
                        reply->readAll().toStdString();
                    const std::string err =
                        ok ? "" : reply->errorString().toStdString();
                    reply->deleteLater();
                    nam->deleteLater();
                    done(payload, err);
                });
    });

    bubble_timer_ = new QTimer(this);
    bubble_timer_->setSingleShot(true);
    connect(bubble_timer_, &QTimer::timeout, this, [this] {
        if (!lesson_active_) {
            bubble_visible_ = false;
            emit bubbleChanged();
        }
    });

    controller_->eventBus().subscribe(
        [this](const blokkily::guide::AppEvent& event) { onBusEvent(event); });

    loadLessons();
}

blokkily::guide::SongSnapshot GuideController::snapshot() const {
    blokkily::guide::SongSnapshot snap;
    snap.pattern_steps = pattern_->stepCount();
    snap.note_count = pattern_->eventCount();
    snap.tempo_bpm = transport_->bpm();
    snap.playing = transport_->playing();
    snap.scale_name = song_->scaleName().toStdString();
    snap.tuning_name = song_->tuningName().toStdString();
    snap.root_name = song_->rootName().toStdString();
    snap.track_count = song_->trackCount();
    const auto parts = splitMeter(transport_->meter());
    if (!parts.isEmpty()) {
        bool ok = false;
        const int beats = parts.front().toInt(&ok);
        if (ok && beats > 0) snap.beats_per_bar = beats;
    }
    return snap;
}

std::string GuideController::sessionContext() const {
    const auto snap = snapshot();
    return std::to_string(static_cast<int>(snap.tempo_bpm)) + " BPM, " +
           std::to_string(snap.note_count) + " notes in the open pattern, " +
           std::to_string(snap.track_count) + " tracks, " + snap.root_name + " " +
           snap.scale_name + " (" + snap.tuning_name + ")";
}

void GuideController::onBusEvent(const blokkily::guide::AppEvent& event) {
    // Lessons watch every event, narrated or not.
    if (lesson_active_ && lessons_.check_event(event)) {
        if (lessons_.at_end()) {
            showBubble("LESSON COMPLETE",
                       "Nicely done — \"" + lessons_.lesson().title +
                           "\" is finished. The technique is yours now.",
                       "guide");
            lesson_active_ = false;
            clearSpotlight();
        } else {
            enterLessonStep();
        }
        emit lessonChanged();
    }
    if (!guide_enabled_ || !narrate_enabled_) return;
    const std::string text = narrator_.describe(event, snapshot());
    if (!text.empty()) showBubble("", QString::fromStdString(text), "guide");
}

void GuideController::showBubble(const QString& title, const QString& text,
                                 const QString& kind) {
    bubble_title_ = title;
    bubble_text_ = text;
    bubble_kind_ = kind;
    bubble_visible_ = true;
    emit bubbleChanged();
    hideBubbleSoon();
}

void GuideController::hideBubbleSoon() {
    bubble_timer_->stop();
    if (!lesson_active_) bubble_timer_->start(7000);
}

void GuideController::say(const QString& title, const QString& text, const QString& kind) {
    if (!guide_enabled_) return;
    showBubble(title, text, kind);
}

void GuideController::dismiss() {
    bubble_timer_->stop();
    bubble_visible_ = false;
    emit bubbleChanged();
}

void GuideController::spotlight(const QString& target) {
    spotlight_target_ = target;
    emit spotlightChanged();
}

void GuideController::clearSpotlight() {
    spotlight_target_.clear();
    emit spotlightChanged();
}

void GuideController::askQuestion(const QString& question) {
    if (!guide_enabled_ || question.trimmed().isEmpty()) return;
    controller_->eventBus().emit(blokkily::guide::AppEvent{
        blokkily::guide::EventSource::user, blokkily::guide::EventKind::question_asked,
        question.toStdString(), ""});
    const std::string ruled =
        narrator_.answer_question(question.toStdString(), snapshot());
    if (!ruled.empty()) {
        showBubble("", QString::fromStdString(ruled), "guide");
        return;
    }
    if (!llm_.enabled()) {
        showBubble("", "I don't have a rule for that one yet — and my LLM help is off. "
                       "Turn it on in the guide's settings and I'll ask a model.",
                   "guide");
        return;
    }
    asking_ = true;
    emit askingChanged();
    llm_.ask(question.toStdString(), sessionContext(),
             [this](const std::string& answer) {
                 asking_ = false;
                 emit askingChanged();
                 showBubble("", QString::fromStdString(answer), "llm");
             },
             [this](const std::string& error) {
                 asking_ = false;
                 emit askingChanged();
                 // Graceful fallback, per design: the rules still work.
                 showBubble("", "The model didn't answer (" +
                                    QString::fromStdString(error) +
                                    "). I can still help from my built-in theory — ask me about triads, cadences or scales.",
                            "guide");
             });
}

void GuideController::loadLessons() {
    lesson_sources_.clear();
    lesson_ids_.clear();
    // Bundled lessons, wherever the QML module's resources landed, plus
    // user-authored ones in the app config dir (so agents can add more).
    QStringList candidates;
    QDirIterator it(":/", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.filePath().contains("guide/lessons/") && it.filePath().endsWith(".json"))
            candidates << it.filePath();
    }
    const QString user_dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/lessons";
    QDir dir(user_dir);
    for (const auto& file : dir.entryList({"*.json"}, QDir::Files))
        candidates << dir.filePath(file);
    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const std::string json = file.readAll().toStdString();
        blokkily::guide::LessonEngine probe;
        std::string error;
        if (!probe.load(json, &error)) continue;
        const std::string id = probe.lesson().id;
        if (lesson_sources_.count(id)) continue;
        lesson_sources_[id] = json;
        lesson_ids_ << QString::fromStdString(id);
    }
    emit lessonsLoaded();
}

QString GuideController::lessonTitle() const {
    return lesson_active_ ? QString::fromStdString(lessons_.lesson().title) : QString();
}
QString GuideController::lessonStepTitle() const {
    return lesson_active_ ? QString::fromStdString(lessons_.step().title) : QString();
}
QString GuideController::lessonStepText() const {
    return lesson_active_ ? QString::fromStdString(lessons_.step().text) : QString();
}
int GuideController::lessonStepIndex() const {
    return lesson_active_ ? static_cast<int>(lessons_.step_index()) : -1;
}
int GuideController::lessonStepCount() const {
    return lesson_active_ ? static_cast<int>(lessons_.lesson().steps.size()) : 0;
}

void GuideController::startLesson(const QString& id) {
    const auto it = lesson_sources_.find(id.toStdString());
    if (it == lesson_sources_.end()) return;
    std::string error;
    if (!lessons_.load(it->second, &error)) return;
    lessons_.start();
    lesson_active_ = true;
    emit lessonChanged();
    enterLessonStep();
}

QString GuideController::lessonTitleFor(const QString& id) {
    const auto it = lesson_sources_.find(id.toStdString());
    if (it == lesson_sources_.end()) return id;
    blokkily::guide::LessonEngine probe;
    std::string error;
    if (!probe.load(it->second, &error)) return id;
    return QString::fromStdString(probe.lesson().title);
}

void GuideController::enterLessonStep() {
    const auto& step = lessons_.step();
    spotlight(QString::fromStdString(step.spotlight));
    bubble_title_ = QString::fromStdString(lessons_.lesson().title) + " · " +
                    QString::fromStdString(step.title);
    bubble_kind_ = "guide";
    if (step.has_action) {
        ControllerSink sink(controller_, pattern_);
        blokkily::guide::ActionDispatcher::Args args;
        for (const auto& [k, v] : step.action.args) args[k] = v;
        const auto result = dispatcher_.dispatch(step.action.id, args, sink);
        bubble_text_ = QString::fromStdString(step.text + "\n\n" + result.message);
    } else {
        bubble_text_ = QString::fromStdString(step.text);
    }
    bubble_visible_ = true;
    emit bubbleChanged();
    emit lessonChanged();
    bubble_timer_->stop(); // lesson bubbles stay until the step completes
}

void GuideController::lessonNext() {
    if (!lesson_active_) return;
    lessons_.next();
    if (lessons_.at_end()) {
        stopLesson();
        return;
    }
    enterLessonStep();
}

void GuideController::lessonBack() {
    if (!lesson_active_) return;
    lessons_.back();
    enterLessonStep();
}

void GuideController::stopLesson() {
    lesson_active_ = false;
    clearSpotlight();
    emit lessonChanged();
}

void GuideController::performGuideAction(const QString& id, const QVariantMap& args) {
    if (!guide_enabled_) return;
    ControllerSink sink(controller_, pattern_);
    blokkily::guide::ActionDispatcher::Args plain;
    for (auto it = args.begin(); it != args.end(); ++it)
        plain[it.key().toStdString()] = it.value().toString().toStdString();
    const auto result = dispatcher_.dispatch(id.toStdString(), plain, sink);
    if (result.needs_confirm) {
        pending_action_ = id.toStdString();
        pending_args_ = plain;
        confirm_text_ = QString::fromStdString(result.message);
        confirm_visible_ = true;
        emit confirmChanged();
        return;
    }
    if (!result.message.empty()) showBubble("", QString::fromStdString(result.message), "guide");
}

void GuideController::confirmPending() {
    confirm_visible_ = false;
    emit confirmChanged();
    if (pending_action_.empty()) return;
    ControllerSink sink(controller_, pattern_);
    const auto result = dispatcher_.dispatch(pending_action_, pending_args_, sink, true);
    pending_action_.clear();
    pending_args_.clear();
    if (!result.message.empty()) showBubble("", QString::fromStdString(result.message), "guide");
}

void GuideController::cancelPending() {
    confirm_visible_ = false;
    pending_action_.clear();
    pending_args_.clear();
    emit confirmChanged();
}

void GuideController::setGuideEnabled(bool on) {
    if (guide_enabled_ == on) return;
    guide_enabled_ = on;
    QSettings("Blokkily", "blokkily").setValue("guide/enabled", on);
    if (!on) {
        dismiss();
        stopLesson();
        clearSpotlight();
    }
    emit guideEnabledChanged();
}

void GuideController::setNarrateEnabled(bool on) {
    if (narrate_enabled_ == on) return;
    narrate_enabled_ = on;
    QSettings("Blokkily", "blokkily").setValue("guide/narrate", on);
    emit narrateEnabledChanged();
}

void GuideController::configureLlm(const QString& endpoint, const QString& key) {
    const std::string ep = endpoint.toStdString();
    const std::string k = key.toStdString();
    // Stored in the app's own settings only — never the repo, never chat.
    QSettings settings("Blokkily", "blokkily");
    settings.setValue("guide/llmEndpoint", endpoint);
    settings.setValue("guide/llmKey", key);
    // Rebuild the client so the transport closure below captures the new values.
    endpoint_ = endpoint;
    api_key_ = key;
    llm_.configure(ep, k);
    emit llmChanged();
}

void GuideController::clearLlm() {
    QSettings settings("Blokkily", "blokkily");
    settings.remove("guide/llmEndpoint");
    settings.remove("guide/llmKey");
    endpoint_.clear();
    api_key_.clear();
    llm_.clear();
    emit llmChanged();
}
