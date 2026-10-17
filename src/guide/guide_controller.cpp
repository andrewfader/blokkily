#include "guide_controller.hpp"

#include "../llm/llm_backend.hpp"
#include "app_controller.hpp"
#include "llm_model.hpp"
#include "pattern_model.hpp"
#include "song_model.hpp"
#include "transport.hpp"

#include "blokkily/guide/narrator.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

// The sink the dispatcher's actions run through: the same SongModel,
// AppController and PatternModel methods the QML buttons call. What the guide
// does is stamped as the guide's for the length of the action, and whoever
// was acting before - the producer, or an agent through MCP - is put back
// afterwards. The guide writes only onto empty steps: it never overwrites
// what the producer has played.
class ControllerSink : public blokkily::guide::ICommandSink {
public:
    ControllerSink(AppController* controller, SongModel* song, PatternModel* pattern)
        : controller_(controller), song_(song), pattern_(pattern) {}

    bool addNote(int step, int key) override {
        if (!free(step) || key < 0 || key > 127) return false;
        const Acting acting(*this);
        pattern_->setStepKey(step, key);
        return pattern_->hasStep(step);
    }
    bool addChord(int step, int root, const std::vector<std::int16_t>& intervals) override {
        if (!free(step) || intervals.empty()) return false;
        for (const auto interval : intervals)
            if (root + interval < 0 || root + interval > 127) return false;
        const Acting acting(*this);
        blokkily::Chord chord;
        chord.root = static_cast<std::int16_t>(root);
        chord.intervals = intervals;
        pattern_->placeChord(step, chord);
        return pattern_->hasStep(step);
    }
    bool removeNote(int step) override {
        if (!pattern_->hasStep(step)) return false;
        const Acting acting(*this);
        pattern_->clearStep(step);
        return true;
    }
    bool setTempo(double bpm) override {
        const Acting acting(*this);
        controller_->setTempo(bpm);
        return true;
    }
    void togglePlayback() override {
        const Acting acting(*this);
        controller_->togglePlayback();
    }
    void addTrack() override {
        const Acting acting(*this);
        controller_->addTrack();
    }
    bool selectInstrument(int index) override {
        const Acting acting(*this);
        return controller_->selectInstrument(index);
    }
    bool transposeSelection(int semitones) override {
        const int selected = pattern_->selectedStep();
        if (selected < 0 || !pattern_->hasStep(selected)) return false;
        const Acting acting(*this);
        pattern_->transposeSelected(semitones);
        return true;
    }
    bool clearPattern() override {
        bool any = false;
        const Acting acting(*this);
        // One step of undo for the whole pattern, as the guide announced it.
        song_->beginGesture();
        for (int step = 0; step < pattern_->stepCount(); ++step) {
            if (!pattern_->hasStep(step)) continue;
            pattern_->clearStep(step);
            any = true;
        }
        song_->endGesture();
        return any;
    }

private:
    bool free(int step) const { return step >= 0 && step < pattern_->stepCount() && !pattern_->hasStep(step); }

    class Acting {
    public:
        explicit Acting(ControllerSink& sink)
            : sink_(sink), controller_(sink.controller_->eventSource()), pattern_(sink.pattern_->eventSource()) {
            sink_.controller_->setEventSource(blokkily::guide::EventSource::guide);
            sink_.pattern_->setEventSource(blokkily::guide::EventSource::guide);
        }
        ~Acting() {
            sink_.controller_->setEventSource(controller_);
            sink_.pattern_->setEventSource(pattern_);
        }
        Acting(const Acting&) = delete;
        Acting& operator=(const Acting&) = delete;

    private:
        ControllerSink& sink_;
        blokkily::guide::EventSource controller_;
        blokkily::guide::EventSource pattern_;
    };

    AppController* controller_;
    SongModel* song_;
    PatternModel* pattern_;
};

} // namespace

GuideController::GuideController(AppController* controller, SongModel* song,
                                 PatternModel* pattern, Transport* transport,
                                 QObject* parent, bool persist)
    : QObject(parent),
      controller_(controller),
      song_(song),
      pattern_(pattern),
      transport_(transport),
      dispatcher_(controller->eventBus()) {
    persist_ = persist;
    if (persist_) {
        QSettings settings("Blokkily", "blokkily");
        guide_enabled_ = settings.value("guide/enabled", false).toBool();
        narrate_enabled_ = settings.value("guide/narrate", true).toBool();
        endpoint_ = settings.value("guide/llmEndpoint").toString();
        api_key_ = settings.value("guide/llmKey").toString();
        ask_model_ = settings.value("guide/askModel", false).toBool();
    }
    if (!endpoint_.isEmpty() && !api_key_.isEmpty())
        llm_.configure(endpoint_.toStdString(), api_key_.toStdString());

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

    narration_timer_ = new QTimer(this);
    narration_timer_->setSingleShot(true);
    connect(narration_timer_, &QTimer::timeout, this, [this] {
        if (guide_enabled_ && !lesson_active_ && !pending_narration_.isEmpty())
            showBubble("", pending_narration_, "guide");
        pending_narration_.clear();
    });
    ask_timer_ = new QTimer(this);
    ask_timer_->setSingleShot(true);
    connect(ask_timer_, &QTimer::timeout, this, [this] {
        finishAsking(ask_generation_, "The model took too long to answer. I can still help from my built-in "
                                      "theory - ask me about triads, cadences or scales.", "guide");
    });

    bubble_timer_ = new QTimer(this);
    bubble_timer_->setSingleShot(true);
    connect(bubble_timer_, &QTimer::timeout, this, [this] {
        if (!lesson_active_) {
            bubble_visible_ = false;
            emit bubbleChanged();
        }
    });

    subscription_ = controller_->eventBus().subscribe(
        [this](const blokkily::guide::AppEvent& event) { onBusEvent(event); });

    loadLessons();
}

blokkily::guide::SongSnapshot GuideController::snapshot() const {
    blokkily::guide::SongSnapshot snap;
    snap.pattern_steps = pattern_->stepCount();
    snap.note_count = pattern_->rowCount();
    for (const auto& slot : song_->song().patterns)
        snap.song_note_count += static_cast<int>(slot.pattern.events().size());
    snap.tempo_bpm = transport_->bpm();
    snap.playing = transport_->playing();
    snap.scale_name = song_->scaleName().toStdString();
    snap.tuning_name = song_->tuningName().toStdString();
    snap.root_name = song_->rootName().toStdString();
    snap.track_count = song_->trackCount();
    const auto meter = transport_->meter().meter_in(0);
    if (meter.numerator > 0) snap.beats_per_bar = meter.numerator;
    return snap;
}

std::string GuideController::sessionContext() const {
    const auto snap = snapshot();
    return std::to_string(static_cast<int>(snap.tempo_bpm)) + " BPM, " +
           std::to_string(snap.note_count) + " notes in the open pattern, " +
           std::to_string(snap.track_count) + " tracks, " + snap.root_name + " " +
           snap.scale_name + " (" + snap.tuning_name + ")";
}

GuideController::~GuideController() {
    controller_->eventBus().unsubscribe(subscription_);
}

void GuideController::onBusEvent(const blokkily::guide::AppEvent& event) {
    if (lesson_active_) {
        // A lesson watches every event; while it runs, its own words stay on
        // screen instead of the narration.
        const auto step = lessons_.step_index();
        if (lessons_.check_event(event))
            // Entering the next step can run its action, which publishes to
            // the bus - not allowed from inside the bus's own delivery.
            QTimer::singleShot(0, this, [this, step] { afterLessonEvent(step); });
        return;
    }
    if (!guide_enabled_ || !narrate_enabled_) return;
    const std::string text = narrator_.describe(event, snapshot());
    if (text.empty()) return;
    pending_narration_ = QString::fromStdString(text);
    narration_timer_->start(150);
}

void GuideController::afterLessonEvent(std::size_t step_before) {
    if (!lesson_active_) return;
    if (lessons_.at_end()) {
        lesson_active_ = false;
        clearSpotlight();
        showBubble(QStringLiteral("LESSON COMPLETE"),
                   QStringLiteral("Nicely done - \"") + QString::fromStdString(lessons_.lesson().title) +
                       QStringLiteral("\" is finished. The technique is yours now."),
                   QStringLiteral("guide"));
        emit lessonChanged();
        return;
    }
    if (lessons_.step_index() != step_before) {
        enterLessonStep();
        return;
    }
    // Counted, not yet done: the step shows how far along it is.
    emit lessonChanged();
}

QString GuideController::lessonProgress() const {
    if (!lesson_active_ || lessons_.at_end() || lessons_.step().count <= 1) return {};
    return QStringLiteral("%1 of %2").arg(lessons_.progress()).arg(lessons_.step().count);
}

void GuideController::setAskModel(bool on) {
    if (ask_model_ == on) return;
    ask_model_ = on;
    if (persist_) QSettings("Blokkily", "blokkily").setValue("guide/askModel", on);
    emit llmChanged();
}

void GuideController::finishAsking(quint64 generation, const QString& text, const QString& kind) {
    if (generation != ask_generation_ || !asking_) return;
    ask_timer_->stop();
    asking_ = false;
    emit askingChanged();
    showBubble("", text, kind);
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

void GuideController::open() {
    if (!guide_enabled_) return;
    showBubble("", QStringLiteral("Ask me something, or press TEACH ME and I'll walk you through making "
                                  "your first beat."),
               "guide");
    bubble_timer_->stop();
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

QVariantList GuideController::narratedEvents(int count) {
    QVariantList events;
    if (count <= 0) return events;
    const auto tail = controller_->eventBus().tail(static_cast<std::size_t>(std::min(count, 50)));
    const auto snapshot = this->snapshot();
    for (const auto& event : tail) {
        const std::string narration = narrator_.describe(event, snapshot);
        if (narration.empty()) continue;
        events.append(QVariantMap{{"source", QString::fromStdString(blokkily::guide::to_string(event.source))},
                                  {"kind", QString::fromStdString(blokkily::guide::to_string(event.kind))},
                                  {"label", QString::fromStdString(event.label)},
                                  {"narration", QString::fromStdString(narration)}});
    }
    return events;
}

QString GuideController::ruleAnswer(const QString& question) {
    return QString::fromStdString(narrator_.answer_question(question.toStdString(), snapshot()));
}

void GuideController::askQuestion(const QString& question) {
    if (!guide_enabled_ || question.trimmed().isEmpty()) return;
    controller_->eventBus().publish(blokkily::guide::AppEvent{
        blokkily::guide::EventSource::user, blokkily::guide::EventKind::question_asked,
        question.toStdString(), ""});
    const std::string ruled = narrator_.answer_question(question.toStdString(), snapshot());
    if (!ruled.empty()) {
        showBubble("", QString::fromStdString(ruled), "guide");
        return;
    }
    const QString fallback = QStringLiteral(
        "The model didn't answer (%1). I can still help from my built-in theory - ask me about "
        "triads, cadences or scales.");
    const quint64 generation = ++ask_generation_;
    if (llm_.enabled()) {
        asking_ = true;
        emit askingChanged();
        ask_timer_->start(30000);
        llm_.ask(question.toStdString(), sessionContext(),
                 [this, generation](const std::string& answer) {
                     finishAsking(generation, QString::fromStdString(answer), "llm");
                 },
                 [this, generation, fallback](const std::string& error) {
                     finishAsking(generation, fallback.arg(QString::fromStdString(error)), "guide");
                 });
        return;
    }
    // The prompt bar's model, only once the producer has said the guide may
    // use it: it may be a cloud service, and the question and a summary of
    // the session go with it.
    blokkily::llm::LlmBackend* shared = llm_model_ != nullptr ? llm_model_->backend() : nullptr;
    if (shared != nullptr && ask_model_) {
        asking_ = true;
        emit askingChanged();
        ask_timer_->start(30000);
        blokkily::llm::Request request;
        // The backends answer in JSON; the answer is asked for as one field.
        request.systemPrompt = QStringLiteral(
            "You are the floating guide in Blokkily, a pattern-first DAW. Answer the producer's "
            "music-theory or composition question briefly, in plain language, with one concrete "
            "musical example. Reply with a JSON object {\"answer\": \"...\"} and nothing else.");
        request.context = QString::fromStdString(sessionContext());
        request.prompt = question;
        const QPointer<GuideController> self(this);
        shared->complete(request, [self, generation, fallback](blokkily::llm::Reply reply) {
            if (self.isNull()) return;
            QString answer;
            if (reply.ok) {
                const auto document = QJsonDocument::fromJson(reply.text.toUtf8());
                answer = document.isObject() ? document.object().value("answer").toString() : reply.text.trimmed();
            }
            if (answer.isEmpty())
                self->finishAsking(generation, fallback.arg(reply.ok ? QStringLiteral("an empty answer") : reply.error),
                                   "guide");
            else
                self->finishAsking(generation, answer, "llm");
        });
        return;
    }
    showBubble("", shared != nullptr
                       ? QStringLiteral("I don't have a rule for that one. Switch on ASK MODEL beside the "
                                        "question box and I'll ask the prompt bar's model.")
                       : QStringLiteral("I don't have a rule for that one yet, and there is no model to ask."),
               "guide");
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
    // A --verify run keeps to the bundled lessons, whatever the user has.
    if (persist_) {
        const QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/lessons");
        for (const auto& file : dir.entryList({"*.json"}, QDir::Files)) candidates << dir.filePath(file);
    }
    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const std::string json = file.readAll().toStdString();
        blokkily::guide::LessonEngine probe;
        std::string error;
        if (!probe.load(json, &error)) {
            qWarning("Guide: lesson %s skipped: %s", qPrintable(path), error.c_str());
            continue;
        }
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
    // A lesson is taught in the overlay: starting one shows the guide.
    setGuideEnabled(true);
    narration_timer_->stop();
    pending_narration_.clear();
    lessons_.start();
    acted_.assign(lessons_.lesson().steps.size(), false);
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
    const auto index = lessons_.step_index();
    if (step.has_action && index < acted_.size() && !acted_[index]) {
        acted_[index] = true;
        ControllerSink sink(controller_, song_, pattern_);
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
    if (!lesson_active_) return;
    lesson_active_ = false;
    clearSpotlight();
    emit lessonChanged();
    hideBubbleSoon();
}

void GuideController::performGuideAction(const QString& id, const QVariantMap& args) {
    if (!guide_enabled_) return;
    ControllerSink sink(controller_, song_, pattern_);
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
    ControllerSink sink(controller_, song_, pattern_);
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
    if (persist_) QSettings("Blokkily", "blokkily").setValue("guide/enabled", on);
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
    if (persist_) QSettings("Blokkily", "blokkily").setValue("guide/narrate", on);
    emit narrateEnabledChanged();
}

bool GuideController::configureLlm(const QString& endpoint, const QString& key) {
    const QUrl url(endpoint);
    const bool local = url.host() == "localhost" || url.host() == "127.0.0.1" || url.host() == "::1";
    if (!url.isValid() || (url.scheme() != "https" && !(url.scheme() == "http" && local)) || key.isEmpty())
        return false;
    const std::string ep = endpoint.toStdString();
    const std::string k = key.toStdString();
    // Stored in the app's own settings only — never the repo, never chat.
    if (persist_) {
        QSettings settings("Blokkily", "blokkily");
        settings.setValue("guide/llmEndpoint", endpoint);
        settings.setValue("guide/llmKey", key);
    }
    // Rebuild the client so the transport closure below captures the new values.
    endpoint_ = endpoint;
    api_key_ = key;
    llm_.configure(ep, k);
    emit llmChanged();
    return true;
}

void GuideController::clearLlm() {
    if (persist_) {
        QSettings settings("Blokkily", "blokkily");
        settings.remove("guide/llmEndpoint");
        settings.remove("guide/llmKey");
    }
    endpoint_.clear();
    api_key_.clear();
    llm_.clear();
    emit llmChanged();
}

bool GuideController::llmEnabled() const {
    if (llm_.enabled()) return true;
    return ask_model_ && llm_model_ != nullptr && llm_model_->backend() != nullptr;
}

void GuideController::setLlmModel(LlmModel* model) {
    llm_model_ = model;
    // A question still out with the old backend is never answered once it
    // goes: the Ask box is handed back rather than waiting for good.
    if (llm_model_ != nullptr)
        connect(llm_model_, &LlmModel::backendChanged, this, [this] {
            finishAsking(ask_generation_, QStringLiteral("The prompt bar's model changed before it answered; "
                                                         "ask again."),
                         "guide");
            emit llmChanged();
        });
    emit llmChanged();
}
