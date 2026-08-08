#include "douyin/LogBridge.h"

#include "douyin/Json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace douyin {
namespace {

std::string Lower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool SamePrefix(std::string_view current, std::string_view expected)
{
    return current.size() >= expected.size() && current.substr(0, expected.size()) == expected;
}

#ifdef _WIN32
bool LooksLikeLiveCompanionLog(const std::filesystem::path& path)
{
    const std::string name = Lower(path.filename().string());
    if (name.find("-client.") != std::string::npos) return false;
    if (name.find(".txt") == std::string::npos && name.find(".log") == std::string::npos) return false;
    return !name.empty() && name.front() >= '0' && name.front() <= '9' && name.find('_') != std::string::npos;
}

std::string EnvironmentValue(const char* name)
{
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string result(value, length > 0 ? length - 1 : 0);
    std::free(value);
    return result;
}
#endif

} // namespace

bool ExtractOpenLiveData(std::string_view line, std::string& callback_json)
{
    constexpr std::size_t MaxLogLineBytes = 1024 * 1024;
    if (line.size() > MaxLogLineBytes) return false;
    const std::size_t capture = line.find("PipeCapture");
    const std::size_t event = line.find("OPEN_LIVE_DATA", capture == std::string_view::npos ? 0 : capture);
    if (capture == std::string_view::npos || event == std::string_view::npos) return false;

    // The event name is inside the captured envelope, so begin at the envelope's
    // opening brace rather than the event-name field itself.
    const std::size_t start = line.find('{', capture + 11);
    if (start == std::string_view::npos) return false;
    bool quoted = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t index = start; index < line.size(); ++index) {
        const char character = line[index];
        if (quoted) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') quoted = false;
            continue;
        }
        if (character == '"') {
            quoted = true;
        } else if (character == '{') {
            ++depth;
        } else if (character == '}' && --depth == 0) {
            const std::string_view candidate = line.substr(start, index - start + 1);
            if (candidate.size() > MaxLogLineBytes) return false;
            json::Value parsed;
            std::string error;
            if (!json::Parse(candidate, parsed, error)) return false;
            callback_json.assign(candidate);
            return true;
        }
    }
    return false;
}

bool IsWebcastMateDisconnect(std::string_view line)
{
    const std::string lower = Lower(line);
    if (lower.find("pipecapture") == std::string::npos) return false;
    return lower.find("disconnected") != std::string::npos
        || lower.find("event_disconnect") != std::string::npos
        || lower.find("session_end") != std::string::npos;
}

std::filesystem::path DiscoverWebcastMateLog()
{
#ifdef _WIN32
    std::vector<std::filesystem::path> roots;
    const std::string local = EnvironmentValue("LOCALAPPDATA");
    const std::string roaming = EnvironmentValue("APPDATA");
    if (!local.empty()) roots.emplace_back(local);
    if (!roaming.empty() && roaming != local) roots.emplace_back(roaming);
    std::filesystem::path best;
    std::filesystem::file_time_type best_time{};
    bool ambiguous = false;
    auto scan = [&](const std::filesystem::path& root) {
        std::error_code error;
        if (!std::filesystem::exists(root, error)) return;
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (!iterator->is_regular_file(error)) continue;
            const std::string name = Lower(iterator->path().filename().string());
            if (name.find(".txt") == std::string::npos && name.find(".log") == std::string::npos) continue;
            std::ifstream input(iterator->path(), std::ios::binary);
            if (!input) continue;
            input.seekg(0, std::ios::end);
            const std::streamoff file_size = input.tellg();
            const std::streamoff start = file_size > 65536 ? file_size - 65536 : 0;
            input.seekg(start, std::ios::beg);
            std::string sample(static_cast<std::size_t>(file_size - start), '\0');
            input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
            if (sample.find("PipeCapture") == std::string::npos
                || sample.find("OPEN_LIVE_DATA") == std::string::npos) continue;
            const auto time = std::filesystem::last_write_time(iterator->path(), error);
            if (!error) {
                if (best.empty() || time > best_time) {
                    best = iterator->path();
                    best_time = time;
                    ambiguous = false;
                } else if (time == best_time) {
                    ambiguous = true;
                }
            }
        }
    };
    // Live Companion stores the capture under webcast_mate/logs; scan those roots first.
    for (const auto& root : roots) {
        const auto log_root = root / "webcast_mate" / "logs";
        std::error_code error;
        if (!std::filesystem::exists(log_root, error)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(log_root, error)) {
            if (error || !entry.is_regular_file(error) || !LooksLikeLiveCompanionLog(entry.path())) continue;
            const auto time = std::filesystem::last_write_time(entry.path(), error);
            if (!error && (best.empty() || time > best_time)) {
                best = entry.path();
                best_time = time;
                ambiguous = false;
            } else if (!error && time == best_time) {
                ambiguous = true;
            }
        }
    }
    if (best.empty()) for (const auto& root : roots) scan(root / "webcast_mate");
    if (best.empty()) for (const auto& root : roots) scan(root);
    return ambiguous ? std::filesystem::path{} : best;
#else
    return {};
#endif
}

std::filesystem::path DiscoverWebcastMateLog(const std::filesystem::path& directory)
{
#ifdef _WIN32
    std::filesystem::path best;
    std::filesystem::file_time_type best_time{};
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!entry.is_regular_file(error) || !LooksLikeLiveCompanionLog(entry.path())) continue;
        const auto time = std::filesystem::last_write_time(entry.path(), error);
        if (!error && (best.empty() || time > best_time)) {
            best = entry.path();
            best_time = time;
        }
    }
    return best;
#else
    (void)directory;
    return {};
#endif
}

bool MessageIdDeduplicator::Accept(std::string_view message_id)
{
    if (message_id.empty() || ids_.find(std::string(message_id)) != ids_.end()) return false;
    if (capacity_ == 0) return true;
    while (order_.size() >= capacity_) {
        ids_.erase(order_.front());
        order_.pop_front();
    }
    order_.emplace_back(message_id);
    ids_.insert(order_.back());
    return true;
}

bool LogTailer::Poll(std::vector<std::string>& records, bool& rotated)
{
    bool disconnected = false;
    return Poll(records, rotated, disconnected);
}

bool LogTailer::Poll(std::vector<std::string>& records, bool& rotated, bool& disconnected)
{
    records.clear();
    rotated = false;
    disconnected = false;
    last_stats_ = {};
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) return true;
    const std::uintmax_t size = std::filesystem::file_size(path_, error);
    if (error) return false;

    std::ifstream input(path_, std::ios::binary);
    if (!input) return false;
    const std::size_t prefix_size = static_cast<std::size_t>(std::min<std::uintmax_t>(size, 256));
    std::string current_prefix(prefix_size, '\0');
    input.read(current_prefix.data(), static_cast<std::streamsize>(prefix_size));
    input.clear();
    if (!initialized_) {
        prefix_ = std::move(current_prefix);
        initialized_ = true;
        last_stats_.initialized = true;
        if (start_at_end_) {
            offset_ = size;
            return true;
        }
    } else if (size < offset_ || (!prefix_.empty() && !SamePrefix(current_prefix, prefix_))) {
        rotated = initialized_;
        last_stats_.rotated = true;
        offset_ = 0;
        pending_.clear();
        prefix_ = std::move(current_prefix);
        initialized_ = true;
    }
    input.seekg(static_cast<std::streamoff>(offset_), std::ios::beg);
    std::string chunk((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    offset_ = size;
    last_stats_.bytes_read = chunk.size();
    pending_ += chunk;
    if (pending_.size() > 1024 * 1024) {
        pending_.clear();
        return true;
    }
    std::size_t line_end = 0;
    while ((line_end = pending_.find('\n')) != std::string::npos) {
        std::string line = pending_.substr(0, line_end);
        pending_.erase(0, line_end + 1);
        ++last_stats_.complete_lines;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        disconnected = disconnected || IsWebcastMateDisconnect(line);
        if (line.find("PipeCapture") != std::string::npos) ++last_stats_.capture_lines;
        std::string callback;
        if (ExtractOpenLiveData(line, callback)) {
            records.push_back(std::move(callback));
            ++last_stats_.extracted_records;
        }
    }
    return true;
}

} // namespace douyin
