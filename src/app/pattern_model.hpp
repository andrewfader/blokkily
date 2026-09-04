#pragma once

#include "song_model.hpp"

#include "blokkily/model/pattern.hpp"
#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QVariantList>

// The canonical pattern, projected for every editor at once. The step grid, the
// tracker, and the piano roll all read from this one object; `steps` is the
// shared row projection the grid and the tracker each render differently.
class PatternModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int eventCount READ rowCount NOTIFY patternChanged)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY patternChanged)
    Q_PROPERTY(int selectedStep READ selectedStep NOTIFY patternChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY patternChanged)
    // Pitch window the piano roll draws, widened to whatever the pattern uses.
    Q_PROPERTY(int lowKey READ lowKey NOTIFY patternChanged)
    Q_PROPERTY(int highKey READ highKey NOTIFY patternChanged)

public:
    static constexpr int step_count = 16;
    static constexpr blokkily::Tick ticks_per_step = 120;
    enum Role { IdRole = Qt::UserRole + 1, StepRole, KeyRole, NameRole, DurationRole,
                VelocityRole, LockRole };

    explicit PatternModel(SongModel* song, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList steps() const;
    QVariantMap selected() const;
    int selectedStep() const noexcept { return selected_step_; }
    int lowKey() const;
    int highKey() const;

    Q_INVOKABLE void toggleStep(int step, int key = 60);
    Q_INVOKABLE bool hasStep(int step) const;
    Q_INVOKABLE void selectStep(int step);

    // Step inspector edits. Each one rewrites the selected trigger in place, so
    // every projection updates from the same change.
    Q_INVOKABLE void transposeSelected(int semitones);
    Q_INVOKABLE void setSelectedVelocity(double velocity);
    Q_INVOKABLE void setSelectedProbability(double probability);
    Q_INVOKABLE void setSelectedRatchets(int ratchets);
    Q_INVOKABLE void setSelectedMicroOffset(int ticks);
    Q_INVOKABLE void setSelectedPlayOnLoop(int loop);
    Q_INVOKABLE void setSelectedLock(int index, double value, bool modulation);
    Q_INVOKABLE void clearSelectedLock();

    const blokkily::Pattern& pattern() const;
    void replace(blokkily::Pattern pattern);
    // Re-reads the song after the current pattern or the whole session changed.
    void refresh();

signals:
    void patternChanged();

private:
    const blokkily::Trigger* triggerAt(int step) const;
    void mutate(int step, const std::function<void(blokkily::Trigger&)>& edit);

    blokkily::Pattern& pattern();
    SongModel* song_ = nullptr;
    int selected_step_ = -1;
};

// Drives the playhead every editor shares. Playback advances from a monotonic
// clock while playing; `locate` moves it deterministically without one.
class Transport final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY changed)
    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(double stepFraction READ stepFraction NOTIFY changed)
    Q_PROPERTY(QString position READ position NOTIFY changed)
    Q_PROPERTY(int bar READ bar NOTIFY changed)
    Q_PROPERTY(int bars READ bars NOTIFY changed)

public:
    // One bar is sixteen steps: the step grid is a bar of sixteenth notes.
    static constexpr int steps_per_bar = PatternModel::step_count;

    explicit Transport(QObject* parent = nullptr);
    bool playing() const noexcept { return playing_; }
    double bpm() const noexcept { return bpm_; }
    // Where the editors' shared playhead sits inside the bar they show.
    int step() const noexcept { return static_cast<int>(step_position_) % steps_per_bar; }
    double stepFraction() const noexcept { return step_position_; }
    // Where the song playhead sits in the arrangement.
    int bar() const noexcept { return static_cast<int>(step_position_) / steps_per_bar; }
    int bars() const noexcept { return bars_; }
    QString position() const;
    void setSongBars(int bars);
    // Slaves the playhead to the audio engine, so what is drawn is where the
    // song actually is rather than a second clock running alongside it.
    void followSamples(std::uint64_t samples, double sample_rate);
    void releaseFollowing();

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void rewind();
    Q_INVOKABLE void locate(double step);
    void setBpm(double bpm);

signals:
    void changed();

private:
    void tick();

    QTimer timer_;
    QElapsedTimer clock_;
    bool playing_ = false;
    bool following_ = false;
    int bars_ = 8;
    double bpm_ = 120.0;
    double step_position_ = 0.0;
};

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QString soundfontStatus READ soundfontStatus NOTIFY soundfontStatusChanged)
    Q_PROPERTY(QString projectStatus READ projectStatus NOTIFY projectStatusChanged)
    Q_PROPERTY(QString projectDetail READ projectDetail NOTIFY projectStatusChanged)
    Q_PROPERTY(QString activeInstrument READ activeInstrument NOTIFY activeInstrumentChanged)
    Q_PROPERTY(bool audioReady READ audioReady NOTIFY activeInstrumentChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY exportStatusChanged)

public:
    explicit AppController(SongModel* song = nullptr, PatternModel* pattern = nullptr,
                           Transport* transport = nullptr, QObject* parent = nullptr);
    QString status() const { return status_; }
    QVariantList plugins() const { return plugins_; }
    QString soundfontStatus() const { return soundfont_status_; }
    QString projectStatus() const { return project_status_; }
    QString projectDetail() const { return project_detail_; }
    QString activeInstrument() const;
    QString exportStatus() const { return export_status_; }
    bool audioReady() const noexcept { return engine_ != nullptr; }
    Q_INVOKABLE void scanPlugins();
    // Loads the chosen instrument onto the selected mixer track.
    Q_INVOKABLE bool selectInstrument(int index);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE bool saveProjectFile(const QString& path);
    Q_INVOKABLE bool loadProjectFile(const QString& path);
    // Bounces the arrangement through the engine the speakers hear.
    Q_INVOKABLE bool exportAudioFile(const QString& path, const QString& depth = "FLOAT32");
    Q_INVOKABLE void setTempo(double bpm);
    void scanPluginPaths(const std::vector<std::filesystem::path>& clap_paths,
                         const std::vector<std::filesystem::path>& vst3_paths,
                         const std::vector<std::filesystem::path>& soundfont_paths);
    bool scanClapFile(const QString& path);
    bool verifyClap(const QString& path);
    bool verifyVst3(const QString& path);
    bool verifyParameterLocks(const QString& clap_path);
    bool verifySoundFont(const QString& path);
    bool verifyMixer();
    bool verifyBounce(const QString& path);
    bool saveProject(const QString& path);
    bool loadProject(const QString& path);
    // Every mixer track that carries an instrument.
    std::vector<blokkily::InstrumentSlot> instruments() const;
    blokkily::SongEngine* engine() const noexcept { return engine_.get(); }

signals:
    void statusChanged();
    void pluginsChanged();
    void soundfontStatusChanged();
    void projectStatusChanged();
    void activeInstrumentChanged();
    void exportStatusChanged();

private:
    // Instantiates one track's instrument through the format adapters.
    std::unique_ptr<blokkily::PluginInstance> createInstrument(
        const blokkily::InstrumentSlot& slot, std::string* error) const;
    // Rebuilds the audio graph for the current arrangement. Mixer moves do not
    // come through here: they are applied to the running engine.
    bool rebuildEngine();
    void applyMix();
    void pollMeters();
    void assignInstrument(int track, const blokkily::InstrumentSlot& slot,
                          const QString& label);

    QString status_ = "Ready — CLAP native";
    QVariantList plugins_;
    QString soundfont_status_ = "No instrument loaded";
    QString project_status_ = "Not saved";
    QString project_detail_ = "No project on disk";
    QString export_status_ = "Not exported";
    SongModel* song_ = nullptr;
    PatternModel* pattern_ = nullptr;
    Transport* transport_ = nullptr;
    std::unique_ptr<blokkily::SongEngine> engine_;
    std::unique_ptr<blokkily::RtAudioOutput> audio_output_;
    QTimer meter_timer_;
};
