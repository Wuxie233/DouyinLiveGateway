#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace douyin {

struct TailPollStats {
    std::size_t bytes_read = 0;
    std::size_t complete_lines = 0;
    std::size_t capture_lines = 0;
    std::size_t extracted_records = 0;
    bool initialized = false;
    bool rotated = false;
};

// Extracts a bounded JSON object without retaining the raw log line.
bool ExtractOpenLiveData(std::string_view line, std::string& callback_json);
bool IsWebcastMateDisconnect(std::string_view line);
std::filesystem::path DiscoverWebcastMateLog();
std::filesystem::path DiscoverWebcastMateLog(const std::filesystem::path& directory);

class MessageIdDeduplicator {
public:
    explicit MessageIdDeduplicator(std::size_t capacity = 8192) : capacity_(capacity) {}
    bool Accept(std::string_view message_id);
    std::size_t Size() const { return ids_.size(); }

private:
    std::size_t capacity_;
    std::deque<std::string> order_;
    std::unordered_set<std::string> ids_;
};

class LogTailer {
public:
    explicit LogTailer(std::filesystem::path path, bool start_at_end = false)
        : path_(std::move(path)), start_at_end_(start_at_end) {}

    // Appends complete extracted callback JSON objects to records. Returns false only on I/O error.
    bool Poll(std::vector<std::string>& records, bool& rotated);
    bool Poll(std::vector<std::string>& records, bool& rotated, bool& disconnected);
    const std::filesystem::path& Path() const { return path_; }
    const TailPollStats& LastStats() const { return last_stats_; }

private:
    std::filesystem::path path_;
    std::uintmax_t offset_ = 0;
    std::string pending_;
    std::string prefix_;
    bool initialized_ = false;
    bool start_at_end_ = false;
    TailPollStats last_stats_;
};

} // namespace douyin
