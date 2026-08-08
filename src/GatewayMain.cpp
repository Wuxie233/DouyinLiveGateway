#include "douyin/Arguments.h"
#include "douyin/Broadcast.h"
#include "douyin/Json.h"
#include "douyin/LiveDataMapper.h"
#include "douyin/LogBridge.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
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
    if (!douyin::ParseArguments(args, options, error)) {
        std::cerr << "argument error: " << error << "\n";
        std::cerr << "usage: DouyinLiveGateway [--logPath <file>]\n";
        return 2;
    }
    douyin::MessageIdDeduplicator dedup;
    const std::string session = "local-session";
    douyin::BroadcastHub hub;
    douyin::NamedPipeServer server(hub);
#ifdef _WIN32
    if (!server.Start()) {
        std::cerr << "failed to start named-pipe broadcast: " << server.Error() << '\n';
        return 1;
    }
#else
    std::cerr << "DouyinLiveGateway requires Windows x64 for named-pipe broadcast\n";
    return 1;
#endif
    bool session_started = false;
    std::unique_ptr<douyin::LogTailer> tailer;
    for (;;) {
        std::vector<std::string> callbacks;
        bool rotated = false;
        bool disconnected = false;
        if (!tailer) {
            std::filesystem::path source = options.log_path.empty()
                ? douyin::DiscoverWebcastMateLog()
                : std::filesystem::path(options.log_path);
            if (source.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            tailer = std::make_unique<douyin::LogTailer>(std::move(source));
        }
        if (!tailer->Poll(callbacks, rotated, disconnected)) {
            if (options.log_path.empty()) tailer.reset();
            else return 1;
            continue;
        }
        for (const auto& callback : callbacks) {
            const auto mapped = douyin::MapOpenLiveData(callback, session);
            for (const auto& line : mapped.lines) {
                douyin::json::Value event;
                std::string parse_error;
                if (douyin::json::Parse(line, event, parse_error)) {
                    const auto* id = event.Find("event_id");
                    const auto* text = id ? id->AsString() : nullptr;
                    if (text && dedup.Accept(*text)) {
                        if (!session_started) {
                            hub.Publish(douyin::SessionStartLine(session));
                            session_started = true;
                        }
                        hub.Publish(line);
                    }
                }
            }
        }
        if (disconnected) {
            if (session_started) hub.Publish(douyin::SessionEndLine(session));
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
