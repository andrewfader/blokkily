#include "recovery.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

RecoveryStore::RecoveryStore(QString directory) : directory_(std::move(directory)) {}

RecoveryStore::~RecoveryStore() {
    // A window that closes normally has nothing left to recover.
    clear();
    if (lock_) lock_->unlock();
    if (!id_.isEmpty()) QFile::remove(path(id_, ".lock"));
}

QString RecoveryStore::default_directory() {
    const auto configured = qEnvironmentVariable("BLOKKILY_RECOVERY_DIR");
    if (!configured.isEmpty()) return configured;
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return data.isEmpty() ? QString() : data + "/recovery";
}

QString RecoveryStore::path(const QString& id, const char* suffix) const {
    return directory_ + "/" + id + QString::fromLatin1(suffix);
}

QString RecoveryStore::own_id() {
    if (!enabled()) return {};
    if (id_.isEmpty()) {
        QDir().mkpath(directory_);
        // A window that died with nothing unsaved left only its lock.
        const QDir folder(directory_);
        for (const auto& file : folder.entryList({"*.lock"}, QDir::Files)) {
            const auto id = file.chopped(5);
            if (!QFile::exists(path(id, ".blok")) && !live(id)) QFile::remove(folder.filePath(file));
        }
        id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        lock_ = std::make_unique<QLockFile>(path(id_, ".lock"));
        // Never stale while this process lives: the pid in it says so.
        lock_->setStaleLockTime(0);
        (void)lock_->tryLock(0);
    }
    return id_;
}

bool RecoveryStore::write(const QByteArray& text, const QString& project, const QString& title,
                          QString* error) {
    if (!enabled()) return false;
    const auto id = own_id();
    QSaveFile song(path(id, ".blok"));
    if (!song.open(QIODevice::WriteOnly) || song.write(text) != text.size() || !song.commit()) {
        if (error != nullptr) *error = song.errorString();
        return false;
    }
    QSettings info(path(id, ".info"), QSettings::IniFormat);
    info.setValue("project", project);
    info.setValue("title", title);
    info.setValue("saved", QDateTime::currentDateTimeUtc());
    info.sync();
    return info.status() == QSettings::NoError;
}

void RecoveryStore::clear() {
    if (id_.isEmpty()) return;
    QFile::remove(path(id_, ".blok"));
    QFile::remove(path(id_, ".info"));
}

bool RecoveryStore::has_own_entry() const {
    return !id_.isEmpty() && QFile::exists(path(id_, ".blok"));
}

bool RecoveryStore::live(const QString& id) const {
    if (id == id_) return true;
    // A lock nobody holds any more (its process is gone) can be taken; one a
    // running window holds cannot.
    QLockFile probe(path(id, ".lock"));
    probe.setStaleLockTime(0);
    if (!QFile::exists(path(id, ".lock"))) return false;
    if (probe.tryLock(0)) {
        probe.unlock();
        return false;
    }
    return probe.error() == QLockFile::LockFailedError;
}

std::vector<RecoveryStore::Entry> RecoveryStore::abandoned() const {
    std::vector<Entry> entries;
    if (!enabled()) return entries;
    const QDir folder(directory_);
    for (const auto& file : folder.entryList({"*.blok"}, QDir::Files)) {
        const auto id = file.chopped(5);
        if (live(id)) continue;
        QSettings info(path(id, ".info"), QSettings::IniFormat);
        Entry entry{id, info.value("project").toString(), info.value("title").toString(),
                    info.value("saved").toDateTime()};
        if (!entry.saved.isValid()) entry.saved = QFileInfo(folder.filePath(file)).lastModified();
        entries.push_back(entry);
    }
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) { return a.saved > b.saved; });
    return entries;
}

std::optional<QByteArray> RecoveryStore::read(const QString& id) const {
    if (!enabled() || id.isEmpty() || id.contains('/')) return std::nullopt;
    QFile file(path(id, ".blok"));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    return file.readAll();
}

bool RecoveryStore::discard(const QString& id) {
    if (!enabled() || id.isEmpty() || id.contains('/') || live(id)) return false;
    QFile::remove(path(id, ".info"));
    QFile::remove(path(id, ".lock"));
    return QFile::remove(path(id, ".blok"));
}
