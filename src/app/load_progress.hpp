#pragma once

// What the window shows while something it has to wait for is loading
// (features/loading_progress.feature): opening a song, building the
// instruments it plays, exporting it. That work runs on the thread that draws
// the window, because plugins are created, activated and have their state
// streamed there. So the work reports how far it has got, and a report now and
// then lets the window draw a frame of the progress bar.
//
// A frame is a turn of the event loop, which is the one place other work could
// cut in. While a report is drawing one: every key, click and wheel turn is
// dropped (nothing can be started or edited half way through), agents' MCP
// requests are held until the work is done (holdRequests), and the engine work
// a timer or a finished background job would have done is owed instead and
// done once the scope closes (pumping()).
//
// Work quicker than the reveal delay is never shown and never lets a frame in,
// so an ordinary rebuild is exactly what it was before.

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>

class LoadProgress final : public QObject {
    Q_OBJECT
    // A load the window is waiting on is being shown.
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // What is loading ("OPENING SHIBUYA-STATIC") and how far it has got
    // ("FUZZBOI · 5 OF 16").
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    // 0 to 1, or negative while there is nothing to count yet.
    Q_PROPERTY(double fraction READ fraction NOTIFY changed)

public:
    // How long work runs before it is shown, and how often it then draws.
    static constexpr int default_reveal_ms = 250;
    static constexpr int default_frame_ms = 33;

    explicit LoadProgress(QObject* parent = nullptr);
    ~LoadProgress() override;

    // Opens a load for as long as it lives. Scopes nest: the outermost one
    // names the load, and an inner one (the instruments of a song being
    // opened) only reports into it.
    class Scope {
    public:
        Scope(LoadProgress& progress, const QString& title);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        LoadProgress& progress_;
    };

    // How far the open load has got. Does nothing when no load is open, so
    // code that may or may not run inside one reports unconditionally.
    void report(double fraction, const QString& detail = {});

    [[nodiscard]] bool active() const noexcept { return shown_; }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString detail() const { return detail_; }
    [[nodiscard]] double fraction() const noexcept { return fraction_; }
    // Inside a load (shown or not yet).
    [[nodiscard]] bool loading() const noexcept { return depth_ > 0; }
    // A report is letting the event loop run. Whatever runs now cut in
    // between two steps of the load, and must leave the engine alone.
    [[nodiscard]] bool pumping() const noexcept { return pumping_; }
    // How many frames the open (or last) load drew.
    [[nodiscard]] int frames() const noexcept { return frames_; }

    // For the gates: 0 shows a load from its first report and draws a frame
    // at every percent, so what is drawn does not depend on how fast the
    // machine is.
    Q_INVOKABLE void setTiming(int reveal_ms, int frame_ms);
    // Runs `work` once the outermost scope has closed, on the next turn of the
    // event loop: what a cut-in owed the engine.
    void afterLoad(std::function<void()> work);

signals:
    void changed();
    // A frame has just been drawn (for the gates, which inspect it).
    void frameDrawn();
    // Agents' requests wait while this is true.
    void holdRequests(bool hold);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin(const QString& title);
    void end();
    void pump();

    int depth_ = 0;
    bool shown_ = false;
    bool pumping_ = false;
    int frames_ = 0;
    int reveal_ms_ = default_reveal_ms;
    int frame_ms_ = default_frame_ms;
    QString title_;
    QString detail_;
    double fraction_ = -1.0;
    double drawn_fraction_ = -2.0;
    QString drawn_detail_;
    QElapsedTimer since_begin_;
    QElapsedTimer since_frame_;
    std::vector<std::function<void()>> after_;
};
