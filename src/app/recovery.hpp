#pragma once

// Crash recovery (features/autosave.feature). Every running window owns one
// entry in the recovery folder: the song as it last stood unsaved, and a
// note of which project it was and when. A lock file held for the life of
// the window says the entry is live; an entry whose lock is stale is what a
// window that crashed, was killed or lost power left behind, and the next
// launch offers it back. Nothing here runs on the audio thread.

#include <QDateTime>
#include <QLockFile>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class RecoveryStore {
public:
    struct Entry {
        QString id;
        // The project the song was opened from or saved to; empty when it
        // was never saved.
        QString project;
        QString title;
        QDateTime saved;
    };

    // `directory` empty turns recovery off: nothing is written or found.
    explicit RecoveryStore(QString directory);
    ~RecoveryStore();
    RecoveryStore(const RecoveryStore&) = delete;
    RecoveryStore& operator=(const RecoveryStore&) = delete;

    // $BLOKKILY_RECOVERY_DIR, else the user's data folder ("recovery" under
    // QStandardPaths::AppDataLocation).
    [[nodiscard]] static QString default_directory();

    [[nodiscard]] bool enabled() const noexcept { return !directory_.isEmpty(); }
    [[nodiscard]] QString directory() const { return directory_; }
    // This window's entry id, claimed (locked) on first use.
    [[nodiscard]] QString own_id();

    // Writes this window's entry: `text` is a serialized project. Atomic: a
    // crash while writing leaves the previous entry whole.
    bool write(const QByteArray& text, const QString& project, const QString& title,
               QString* error = nullptr);
    // Removes this window's entry: the song is saved, or deliberately let go.
    void clear();
    [[nodiscard]] bool has_own_entry() const;

    // Entries left by windows that are no longer running, newest first.
    [[nodiscard]] std::vector<Entry> abandoned() const;
    // The serialized project of an abandoned entry.
    [[nodiscard]] std::optional<QByteArray> read(const QString& id) const;
    // Takes an abandoned entry for this window by locking it, so a window
    // that started beside this one neither offers nor recovers it as well.
    // False when another window holds it, or it is gone.
    [[nodiscard]] bool claim(const QString& id);
    // Gives a claimed entry back, untouched, for any window to offer.
    void release(const QString& id);
    // Deletes an abandoned entry (after it is recovered, or when declined).
    // An entry a live window holds is never removed, unless it is this
    // window's claim.
    bool discard(const QString& id);

private:
    [[nodiscard]] QString path(const QString& id, const char* suffix) const;
    [[nodiscard]] bool live(const QString& id) const;

    QString directory_;
    QString id_;
    std::unique_ptr<QLockFile> lock_;
    // Abandoned entries this window has claimed, each held by its lock.
    std::map<QString, std::unique_ptr<QLockFile>> claimed_;
};
