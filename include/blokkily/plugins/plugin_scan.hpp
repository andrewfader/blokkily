#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace blokkily {

// One plugin file or bundle waiting to be scanned. Describing a plugin means
// running its code, and a plugin that hangs or crashes while being described
// takes its host down with it, so a candidate is scanned in its own process
// rather than in the process that owns the window.
struct ScanCandidate {
    std::string format;              // "CLAP" or "VST3"
    std::filesystem::path path;
};

// What a plugin is for, as the browser groups it: something that plays notes,
// or something that processes the audio it is given.
inline constexpr std::string_view instrument_kind = "instrument";
inline constexpr std::string_view effect_kind = "effect";

// A plugin the scan found, in the shape the browser lists.
struct ScanRecord {
    std::string format;
    std::string name;
    std::string vendor;
    std::string path;
    std::string identifier;
    int index = 0;
    std::string kind{instrument_kind}; // instrument_kind or effect_kind
};

// The kind a CLAP plugin declares through its features: an audio effect that
// is not also an instrument is an effect; anything else plays notes.
[[nodiscard]] std::string clap_kind(const std::vector<std::string>& features);

// What one scan of one candidate produced, remembered between launches: a
// candidate that failed is skipped next time instead of hanging again, and a
// candidate that succeeded is listed without paying for the scan twice.
struct ScanCacheEntry {
    ScanCandidate candidate;
    std::int64_t stamp = 0;          // last write time, so a replaced plugin rescans
    std::uint64_t size = 0;
    bool ok = true;
    std::string failure;
    std::vector<ScanRecord> records;
};

// Walks the search paths for plugin files. Enumeration only reads directory
// entries: no plugin code runs, so this is safe on the thread drawing the UI.
[[nodiscard]] std::vector<ScanCandidate> enumerate_scan_candidates(
    const std::vector<std::filesystem::path>& clap_paths,
    const std::vector<std::filesystem::path>& vst3_paths);

// Loads one candidate and describes what it holds. This runs plugin code and
// may never return, so it belongs in the scan helper process alone.
[[nodiscard]] std::vector<ScanRecord> scan_candidate(const ScanCandidate& candidate,
                                                     std::string* error = nullptr);

// How the helper's answer travels back to the host, and how both are kept on
// disk: one escaped record per line, so a partial write is a truncated list
// rather than an unreadable file.
[[nodiscard]] std::string write_scan_records(const std::vector<ScanRecord>& records);
[[nodiscard]] std::vector<ScanRecord> read_scan_records(std::string_view text);
[[nodiscard]] std::string write_scan_cache(const std::vector<ScanCacheEntry>& entries);
[[nodiscard]] std::vector<ScanCacheEntry> read_scan_cache(std::string_view text);

// How well a browser entry answers what the producer typed. An installation
// holds hundreds of instruments, so the browser is searched by typing a few
// letters of a name rather than by scrolling: a query matches when its letters
// appear in the entry in order, so "fbs" finds "Fat Bass". A higher score is a
// closer match — letters at the start of the entry or of one of its words beat
// letters found in the middle of one — and -1 is no match at all. An empty
// query matches everything, with the same score, so the list keeps its own
// order until something is typed.
[[nodiscard]] int browser_match_score(std::string_view query, std::string_view text);

// Identity of the file behind a candidate: a plugin that was replaced since it
// was cached is scanned again rather than trusted.
[[nodiscard]] std::int64_t scan_stamp(const std::filesystem::path& path);
[[nodiscard]] std::uint64_t scan_size(const std::filesystem::path& path);

} // namespace blokkily
