#include "douyin/Arguments.h"
#include "douyin/Json.h"
#include "douyin/LiveDataMapper.h"
#include "douyin/LogBridge.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::wstring> args;
    for (int index = 1; index < argc; ++index) {
        std::string value(argv[index]);
        args.emplace_back(value.begin(), value.end());
    }
    douyin::Arguments options;
    std::string error;
    if (!douyin::ParseArguments(args, options, error) || options.log_path.empty()) {
        std::cerr << "usage: DouyinLiveGateway --logPath <file>\n";
        return 2;
    }
    douyin::LogTailer tailer(std::filesystem::path(options.log_path));
    douyin::MessageIdDeduplicator dedup;
    const std::string session = "local-session";
    std::cout << douyin::SessionStartLine(session) << '\n' << std::flush;
    for (;;) {
        std::vector<std::string> callbacks;
        bool rotated = false;
        bool disconnected = false;
        if (!tailer.Poll(callbacks, rotated, disconnected)) return 1;
        for (const auto& callback : callbacks) {
            const auto mapped = douyin::MapOpenLiveData(callback, session);
            for (const auto& line : mapped.lines) {
                douyin::json::Value event;
                std::string parse_error;
                if (douyin::json::Parse(line, event, parse_error)) {
                    const auto* id = event.Find("event_id");
                    const auto* text = id ? id->AsString() : nullptr;
                    if (text && dedup.Accept(*text)) std::cout << line << '\n' << std::flush;
                }
            }
        }
        if (disconnected) { std::cout << douyin::SessionEndLine(session) << '\n' << std::flush; return 0; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
