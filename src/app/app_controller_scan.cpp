#include "app_controller.hpp"
#include "app_controller_internal.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QVariantMap>
#include <QUrl>

#include <cstring>
#include <system_error>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace {
using app_detail::plugin_entry;
}

void AppController::scanPlugins() {
    beginScan(blokkily::ClapCatalog::system_paths(),
              blokkily::Vst3PluginInstance::system_paths(),
              blokkily::SoundFontCatalog::system_paths(), false,
              blokkily::Vst3PluginInstance::lv2_system_paths());
}

void AppController::rescanPlugins() {
    beginScan(blokkily::ClapCatalog::system_paths(),
              blokkily::Vst3PluginInstance::system_paths(),
              blokkily::SoundFontCatalog::system_paths(), true,
              blokkily::Vst3PluginInstance::lv2_system_paths());
}

QString AppController::scanHelperPath() const {
    if (const auto override_path = qEnvironmentVariable("BLOKKILY_SCAN_HELPER");
        !override_path.isEmpty())
        return override_path;
#if defined(Q_OS_WIN)
    return QCoreApplication::applicationDirPath() + "/blokkily_scan.exe";
#else
    return QCoreApplication::applicationDirPath() + "/blokkily_scan";
#endif
}

QString AppController::scanCachePath() const {
    if (const auto override_path = qEnvironmentVariable("BLOKKILY_SCAN_CACHE");
        !override_path.isEmpty())
        return override_path;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           "/plugin-scan-cache.txt";
}

void AppController::loadScanCache() {
    if (scan_cache_loaded_) return;
    scan_cache_loaded_ = true;
    QFile file(scanCachePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const auto text = file.readAll();
    scan_cache_ = blokkily::read_scan_cache(std::string_view(text.constData(),
                                                             static_cast<std::size_t>(text.size())));
}

void AppController::saveScanCache() const {
    const QString path = scanCachePath();
    QDir{}.mkpath(QFileInfo(path).absolutePath());
    // Written aside and renamed into place: every window shares the cache,
    // and one reading it while another writes must see a whole file.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    const auto text = blokkily::write_scan_cache(scan_cache_);
    if (file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())) {
        file.cancelWriting();
        return;
    }
    (void)file.commit();
}

blokkily::ScanCacheEntry* AppController::cachedScan(const blokkily::ScanCandidate& candidate) {
    for (auto& entry : scan_cache_) {
        if (entry.candidate.format != candidate.format ||
            entry.candidate.path != candidate.path)
            continue;
        // A plugin replaced since it was scanned is described again: the cache
        // remembers a file, not a name.
        if (entry.stamp != blokkily::scan_stamp(candidate.path) ||
            entry.size != blokkily::scan_size(candidate.path))
            return nullptr;
        return &entry;
    }
    return nullptr;
}

void AppController::beginScan(const std::vector<std::filesystem::path>& clap_paths,
                              const std::vector<std::filesystem::path>& vst3_paths,
                              const std::vector<std::filesystem::path>& soundfont_paths,
                              bool forget_cache,
                              const std::vector<std::filesystem::path>& lv2_paths) {
    if (scanning_) return;
    loadScanCache();
    if (forget_cache) scan_cache_.clear();

    // SoundFonts are files the host reads itself, so they are listed up front;
    // only formats that mean loading someone else's code go through a helper.
    const auto soundfonts = blokkily::SoundFontCatalog::scan_paths(soundfont_paths);
    plugins_.clear();
    scan_failures_ = 0;
    scan_index_ = 0;
    scan_queue_ = blokkily::enumerate_scan_candidates(clap_paths, vst3_paths, lv2_paths);
    scanning_ = true;
    emit scanningChanged();

    soundfont_status_ = QString("%1 installed SoundFont%2")
                            .arg(soundfonts.size()).arg(soundfonts.size() == 1 ? "" : "s");
    emit soundfontStatusChanged();
    for (const auto& soundfont : soundfonts)
        plugins_.push_back(plugin_entry("SF", soundfont.stem().string(),
                                        soundfont.parent_path().string(), soundfont.string()));
    emit pluginsChanged();
    reportScanProgress();
    // Always through the event loop, even when every candidate is already
    // cached: a caller that connects to `scanFinished` after asking for the
    // scan must not miss it.
    QTimer::singleShot(0, this, &AppController::scanNext);
}

void AppController::appendRecords(const std::vector<blokkily::ScanRecord>& records) {
    if (records.empty()) return;
    for (const auto& record : records)
        plugins_.push_back(plugin_entry(QString::fromStdString(record.format), record.name,
                                        record.vendor, record.path, record.identifier,
                                        record.index, record.kind));
    emit pluginsChanged();
}

double AppController::scanProgress() const {
    if (!scanning_) return -1.0;
    if (scan_queue_.empty()) return 0.0;
    return static_cast<double>(scan_index_) / static_cast<double>(scan_queue_.size());
}

void AppController::reportScanProgress() {
    emit scanProgressChanged();
    status_ = QString("Scanning %1/%2 — %3 found, %4 failure%5")
                  .arg(scan_index_).arg(scan_queue_.size())
                  .arg(plugins_.size()).arg(scan_failures_)
                  .arg(scan_failures_ == 1 ? "" : "s");
    emit statusChanged();
}

void AppController::scanNext() {
    const QString helper = scanHelperPath();
    // Everything the cache already describes reaches the browser at once:
    // listed one plugin at a time, a large installation would have the
    // browser rebuilt once per plugin before the window could draw.
    std::vector<blokkily::ScanRecord> cached_records;
    const auto list_cached = [this, &cached_records] { appendRecords(cached_records); };
    while (scan_index_ < scan_queue_.size()) {
        const auto& candidate = scan_queue_[scan_index_];
        if (const auto* cached = cachedScan(candidate)) {
            // Already described, or already known to be unscannable: either way
            // the plugin is not loaded again.
            if (cached->ok)
                cached_records.insert(cached_records.end(), cached->records.begin(),
                                      cached->records.end());
            else ++scan_failures_;
            ++scan_index_;
            continue;
        }
        if (!QFileInfo(helper).isExecutable()) {
            status_ = QString("Plugin scan helper not found at %1").arg(helper);
            emit statusChanged();
            scan_index_ = scan_queue_.size();
            break;
        }
        list_cached();
        reportScanProgress();
        startScanner(candidate);
        return;
    }
    list_cached();
    finishScan();
}

void AppController::startScanner(const blokkily::ScanCandidate& candidate) {
    scan_expired_ = false;
    scanner_ = std::make_unique<QProcess>();
    scanner_->setProgram(scanHelperPath());
    scanner_->setArguments({QString::fromStdString(candidate.format),
                            QString::fromStdString(candidate.path.string())});
    QObject::connect(scanner_.get(), &QProcess::errorOccurred, this,
                     [this](QProcess::ProcessError error) {
                         if (error != QProcess::FailedToStart) return;
                         completeCandidate(false, "scan helper could not be started", {});
                     });
    QObject::connect(scanner_.get(), &QProcess::finished, this,
                     [this](int code, QProcess::ExitStatus exit_status) {
                         if (scan_expired_)
                             return completeCandidate(false, "scan timed out", {});
                         if (exit_status != QProcess::NormalExit)
                             return completeCandidate(false, "scan crashed", {});
                         const auto output = scanner_->readAllStandardOutput();
                         if (code != 0)
                             return completeCandidate(
                                 false, QString::fromUtf8(scanner_->readAllStandardError()).trimmed(),
                                 {});
                         completeCandidate(true, {},
                             blokkily::read_scan_records(std::string_view(
                                 output.constData(), static_cast<std::size_t>(output.size()))));
                     });
    scan_deadline_.start(qEnvironmentVariableIntValue("BLOKKILY_SCAN_TIMEOUT_MS") > 0
                             ? qEnvironmentVariableIntValue("BLOKKILY_SCAN_TIMEOUT_MS")
                             : 15000);
    scanner_->start();
}

void AppController::completeCandidate(bool ok, const QString& failure,
                                      std::vector<blokkily::ScanRecord> records) {
    scan_deadline_.stop();
    if (scanner_) {
        scanner_->disconnect(this);
        scanner_.release()->deleteLater();
    }
    if (scan_index_ >= scan_queue_.size()) return;

    blokkily::ScanCacheEntry entry;
    entry.candidate = scan_queue_[scan_index_];
    entry.stamp = blokkily::scan_stamp(entry.candidate.path);
    entry.size = blokkily::scan_size(entry.candidate.path);
    entry.ok = ok;
    entry.failure = failure.toStdString();
    entry.records = records;
    std::erase_if(scan_cache_, [&entry](const blokkily::ScanCacheEntry& existing) {
        return existing.candidate.format == entry.candidate.format &&
               existing.candidate.path == entry.candidate.path;
    });
    scan_cache_.push_back(std::move(entry));

    if (ok) appendRecords(records);
    else ++scan_failures_;
    ++scan_index_;
    reportScanProgress();
    // Written as the scan goes, so a session closed halfway through does not
    // throw away what has already been learned.
    saveScanCache();
    // Back to the event loop between candidates, so the interface keeps
    // answering while a long scan works through the queue.
    QTimer::singleShot(0, this, &AppController::scanNext);
}

void AppController::finishScan() {
    scanning_ = false;
    emit scanProgressChanged();
    saveScanCache();
    emit pluginsChanged();
    int clap = 0;
    int vst3 = 0;
    int lv2 = 0;
    int soundfonts = 0;
    // Counted by format, so nothing that is not a SoundFont is counted as one.
    for (const auto& entry : plugins_) {
        const auto format = entry.toMap().value("format").toString();
        if (format == "CLAP") ++clap;
        else if (format == "VST3") ++vst3;
        else if (format == "LV2") ++lv2;
        else if (format == "SF") ++soundfonts;
    }
    status_ = QString("Scan: %1 CLAP, %2 VST3, %3 LV2, %4 SoundFont, %5 failure%6")
                  .arg(clap).arg(vst3).arg(lv2).arg(soundfonts).arg(scan_failures_)
                  .arg(scan_failures_ == 1 ? "" : "s");
    emit statusChanged();
    emit scanningChanged();
    emit scanFinished();
}

void AppController::scanPluginPaths(
    const std::vector<std::filesystem::path>& clap_paths,
    const std::vector<std::filesystem::path>& vst3_paths,
    const std::vector<std::filesystem::path>& soundfont_paths,
    const std::vector<std::filesystem::path>& lv2_paths) {
    // CLAP is the first-class format, so it leads the browser; every installed
    // instrument type is still presented through the same list.
    const auto clap = blokkily::ClapCatalog{}.scan_paths(clap_paths);
    const auto vst3 = blokkily::Vst3PluginInstance::scan_paths(vst3_paths);
    std::vector<blokkily::Vst3Descriptor> lv2;
    for (const auto& candidate : blokkily::enumerate_scan_candidates({}, {}, lv2_paths))
        for (auto& plugin : blokkily::Vst3PluginInstance::scan_lv2(candidate.path))
            lv2.push_back(std::move(plugin));
    const auto soundfonts = blokkily::SoundFontCatalog::scan_paths(soundfont_paths);
    plugins_.clear();
    for (const auto& plugin : clap.plugins)
        plugins_.push_back(plugin_entry("CLAP", plugin.name, plugin.vendor,
                                        plugin.library.string(), plugin.id, 0,
                                        blokkily::clap_kind(plugin.features)));
    for (const auto& plugin : vst3)
        plugins_.push_back(plugin_entry(
            "VST3", plugin.name, plugin.manufacturer, plugin.bundle.string(), plugin.identifier,
            static_cast<int>(plugin.index),
            std::string(plugin.instrument ? blokkily::instrument_kind : blokkily::effect_kind)));
    for (const auto& plugin : lv2)
        plugins_.push_back(plugin_entry(
            "LV2", plugin.name, plugin.manufacturer, plugin.bundle.string(), plugin.identifier,
            static_cast<int>(plugin.index),
            std::string(plugin.instrument ? blokkily::instrument_kind : blokkily::effect_kind)));
    for (const auto& soundfont : soundfonts)
        plugins_.push_back(plugin_entry("SF", soundfont.stem().string(),
                                        soundfont.parent_path().string(), soundfont.string()));
    soundfont_status_ = QString("%1 installed SoundFont%2")
                            .arg(soundfonts.size()).arg(soundfonts.size() == 1 ? "" : "s");
    status_ = QString("Scan: %1 CLAP, %2 VST3, %3 LV2, %4 SoundFont, %5 failure%6")
                  .arg(clap.plugins.size()).arg(vst3.size()).arg(lv2.size())
                  .arg(soundfonts.size()).arg(clap.failures.size())
                  .arg(clap.failures.size() == 1 ? "" : "s");
    emit pluginsChanged();
    emit soundfontStatusChanged();
    emit statusChanged();
}

bool AppController::scanClapFile(const QString& path) {
    const auto result = blokkily::ClapCatalog{}.scan_file(path.toStdString());
    plugins_.clear();
    for (const auto& plugin : result.plugins)
        plugins_.push_back(plugin_entry("CLAP", plugin.name, plugin.vendor, {}, {}, 0,
                                        blokkily::clap_kind(plugin.features)));
    status_ = QString("CLAP fixture: %1 plugin%2, %3 failure%4")
                  .arg(result.plugins.size()).arg(result.plugins.size() == 1 ? "" : "s")
                  .arg(result.failures.size()).arg(result.failures.size() == 1 ? "" : "s");
    emit pluginsChanged();
    emit statusChanged();
    return result.plugins.size() == 1 && result.failures.empty();
}
