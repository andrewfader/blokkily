#include "blokkily/plugins/plugin_scan.hpp"

#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>
#include <tuple>

namespace blokkily {
namespace {

// Version 2 records each plugin's kind (instrument or effect). A version 1
// cache is not read: it would list every effect as an instrument.
constexpr const char* cache_header = "blokkily-scan-cache 2";

std::filesystem::path normalize(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

bool has_extension(const std::filesystem::path& path, std::string_view extension) {
    auto found = path.extension().string();
    std::transform(found.begin(), found.end(), found.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return found == extension;
}

// Fields travel between two processes and back off disk, so a name holding a
// tab or a newline must not turn into another field or another record.
std::string escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '\\': result += "\\\\"; break;
            case '\t': result += "\\t"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            default: result += character;
        }
    }
    return result;
}

std::string unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '\\' || index + 1 == value.size()) {
            result += value[index];
            continue;
        }
        switch (value[++index]) {
            case 't': result += '\t'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            default: result += value[index];
        }
    }
    return result;
}

std::vector<std::string> split_fields(std::string_view line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto separator = line.find('\t', start);
        if (separator == std::string_view::npos) {
            fields.emplace_back(unescape(line.substr(start)));
            return fields;
        }
        fields.emplace_back(unescape(line.substr(start, separator - start)));
        start = separator + 1;
    }
}

template <typename Number>
Number to_number(const std::string& text) {
    Number value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

void append_record(std::string& text, const ScanRecord& record) {
    text += "record\t";
    text += escape(record.format);
    text += '\t';
    text += escape(record.name);
    text += '\t';
    text += escape(record.vendor);
    text += '\t';
    text += escape(record.path);
    text += '\t';
    text += escape(record.identifier);
    text += '\t';
    text += std::to_string(record.index);
    text += '\t';
    text += escape(record.kind);
    text += '\n';
}

bool read_record(const std::vector<std::string>& fields, ScanRecord& record) {
    if (fields.size() != 8 || fields[0] != "record") return false;
    if (fields[7] != instrument_kind && fields[7] != effect_kind) return false;
    record = {fields[1], fields[2], fields[3], fields[4], fields[5], to_number<int>(fields[6]),
              fields[7]};
    return true;
}

// Walks `text` line by line without copying it.
template <typename Visitor>
void for_each_line(std::string_view text, const Visitor& visit) {
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        if (end > start) visit(text.substr(start, end - start));
        start = end + 1;
    }
}

} // namespace

std::vector<ScanCandidate> enumerate_scan_candidates(
    const std::vector<std::filesystem::path>& clap_paths,
    const std::vector<std::filesystem::path>& vst3_paths) {
    std::vector<ScanCandidate> candidates;
    const auto add = [&candidates](const char* format, const std::filesystem::path& path) {
        candidates.push_back({format, path});
    };

    for (const auto& root : clap_paths) {
        std::error_code error;
        const auto normalized = normalize(root);
        if (std::filesystem::is_regular_file(normalized, error) &&
            has_extension(normalized, ".clap")) {
            add("CLAP", normalized);
            continue;
        }
        if (!std::filesystem::is_directory(normalized, error)) continue;
        // A CLAP bundle is a directory of modules, so the walk descends into
        // one rather than treating the directory itself as a plugin.
        for (std::filesystem::recursive_directory_iterator it(
                 normalized, std::filesystem::directory_options::skip_permission_denied, error),
             end; it != end; it.increment(error)) {
            if (error) { error.clear(); continue; }
            if (it->is_regular_file(error) && has_extension(it->path(), ".clap"))
                add("CLAP", it->path());
        }
    }

    for (const auto& root : vst3_paths) {
        std::error_code error;
        // JUCE recognises a bundle by the spelling of its path, so a search
        // path written through the bundle's own contents is resolved first.
        const auto normalized = normalize(root);
        if (has_extension(normalized, ".vst3")) {
            if (std::filesystem::exists(normalized, error)) add("VST3", normalized);
            continue;
        }
        if (!std::filesystem::is_directory(normalized, error)) continue;
        for (std::filesystem::recursive_directory_iterator it(
                 normalized, std::filesystem::directory_options::skip_permission_denied, error),
             end; it != end; it.increment(error)) {
            if (error) { error.clear(); continue; }
            if (!has_extension(it->path(), ".vst3")) continue;
            // A VST3 bundle is one plugin, not a tree to search: its own
            // contents are the plugin's business, so the walk stops here.
            add("VST3", it->path());
            if (it->is_directory(error)) it.disable_recursion_pending();
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const ScanCandidate& left, const ScanCandidate& right) {
                  return std::tie(left.format, left.path) < std::tie(right.format, right.path);
              });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](const ScanCandidate& left, const ScanCandidate& right) {
                                     return left.format == right.format &&
                                            left.path == right.path;
                                 }),
                     candidates.end());
    return candidates;
}

std::vector<ScanRecord> scan_candidate(const ScanCandidate& candidate, std::string* error) {
    std::vector<ScanRecord> records;
    if (candidate.format == "CLAP") {
        const auto result = ClapCatalog{}.scan_file(candidate.path);
        if (!result.failures.empty() && error != nullptr) *error = result.failures.front().reason;
        if (!result.failures.empty()) return records;
        for (const auto& plugin : result.plugins)
            records.push_back({"CLAP", plugin.name, plugin.vendor, plugin.library.string(),
                               plugin.id, 0, clap_kind(plugin.features)});
        return records;
    }
    if (candidate.format == "VST3") {
        for (const auto& plugin : Vst3PluginInstance::scan(candidate.path))
            records.push_back({"VST3", plugin.name, plugin.manufacturer, plugin.bundle.string(),
                               plugin.identifier, static_cast<int>(plugin.index),
                               std::string(plugin.instrument ? instrument_kind : effect_kind)});
        return records;
    }
    if (error != nullptr) *error = "unknown plugin format '" + candidate.format + "'";
    return records;
}

std::string clap_kind(const std::vector<std::string>& features) {
    const auto has = [&features](std::string_view wanted) {
        return std::find(features.begin(), features.end(), wanted) != features.end();
    };
    return std::string(has("audio-effect") && !has("instrument") ? effect_kind
                                                                  : instrument_kind);
}

std::string write_scan_records(const std::vector<ScanRecord>& records) {
    std::string text;
    for (const auto& record : records) append_record(text, record);
    return text;
}

std::vector<ScanRecord> read_scan_records(std::string_view text) {
    std::vector<ScanRecord> records;
    for_each_line(text, [&records](std::string_view line) {
        ScanRecord record;
        if (read_record(split_fields(line), record)) records.push_back(std::move(record));
    });
    return records;
}

std::string write_scan_cache(const std::vector<ScanCacheEntry>& entries) {
    std::string text = cache_header;
    text += '\n';
    for (const auto& entry : entries) {
        text += "candidate\t";
        text += escape(entry.candidate.format);
        text += '\t';
        text += escape(entry.candidate.path.string());
        text += '\t';
        text += std::to_string(entry.stamp);
        text += '\t';
        text += std::to_string(entry.size);
        text += '\t';
        text += entry.ok ? "ok" : "failed";
        text += '\t';
        text += escape(entry.failure);
        text += '\n';
        for (const auto& record : entry.records) append_record(text, record);
    }
    return text;
}

std::vector<ScanCacheEntry> read_scan_cache(std::string_view text) {
    // An unrecognised file is treated as no cache at all, so a format change
    // costs one rescan rather than a wrong plugin list.
    const auto first_line = text.substr(0, text.find('\n'));
    if (first_line != cache_header) return {};
    std::vector<ScanCacheEntry> entries;
    bool header_seen = false;
    for_each_line(text, [&](std::string_view line) {
        if (!header_seen) { header_seen = true; return; }
        const auto fields = split_fields(line);
        if (!fields.empty() && fields[0] == "candidate" && fields.size() == 7) {
            ScanCacheEntry entry;
            entry.candidate = {fields[1], std::filesystem::path(fields[2])};
            entry.stamp = to_number<std::int64_t>(fields[3]);
            entry.size = to_number<std::uint64_t>(fields[4]);
            entry.ok = fields[5] == "ok";
            entry.failure = fields[6];
            entries.push_back(std::move(entry));
            return;
        }
        ScanRecord record;
        if (!entries.empty() && read_record(fields, record))
            entries.back().records.push_back(std::move(record));
    });
    return entries;
}

int browser_match_score(std::string_view query, std::string_view text) {
    const auto fold = [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    };
    const auto is_word = [](char value) {
        return std::isalnum(static_cast<unsigned char>(value)) != 0;
    };
    // Spaces in a query separate letters that need not be adjacent, and are
    // already implied by the match being a subsequence, so they are dropped.
    std::string wanted;
    for (const char character : query)
        if (!std::isspace(static_cast<unsigned char>(character))) wanted += fold(character);
    if (wanted.empty()) return 0;

    int score = 0;
    std::size_t cursor = 0;
    std::size_t previous = std::string_view::npos;
    for (const char character : wanted) {
        while (cursor < text.size() && fold(text[cursor]) != character) ++cursor;
        if (cursor == text.size()) return -1;
        if (cursor == 0)
            score += 10;                                  // the entry starts this way
        else if (!is_word(text[cursor - 1]))
            score += 8;                                   // a word of it starts this way
        else if (previous != std::string_view::npos && cursor == previous + 1)
            score += 6;                                   // it carries on the last letter
        else
            score += 1;                                   // found, but buried
        previous = cursor;
        ++cursor;
    }
    // Two entries matched the same way are separated by how much of the entry
    // the query accounts for, so a short name beats a long one holding it.
    if (!text.empty())
        score += static_cast<int>((10 * wanted.size()) / text.size());
    return score;
}

std::int64_t scan_stamp(const std::filesystem::path& path) {
    std::error_code error;
    const auto written = std::filesystem::last_write_time(path, error);
    if (error) return 0;
    return written.time_since_epoch().count();
}

std::uint64_t scan_size(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::uint64_t>(size);
}

} // namespace blokkily
