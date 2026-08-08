#include "douyin/Arguments.h"
#include "douyin/Json.h"
#include "douyin/LiveDataMapper.h"
#include "douyin/LogBridge.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool value, const char* name) {
    if (!value) { std::cerr << "FAILED: " << name << '\n'; ++failures; }
}
std::string Envelope(std::string payload) {
    return "{\"type\":\"event\",\"eventName\":\"OPEN_LIVE_DATA\",\"params\":{\"payload\":[" + std::move(payload) + "]}}";
}
void TestArguments() {
    douyin::Arguments args;
    std::string error;
    Check(douyin::ParseArguments({L"--logPath", L"capture.log", L"--ignored"}, args, error), "argument parsing");
    Check(args.log_path == L"capture.log", "explicit log path");
    Check(!douyin::ParseArguments({L"--logPath"}, args, error), "missing log path rejected");
}
void TestMappedEvents() {
    const std::string comment = R"({"msg_id":"comment-1","timestamp":1711939362000,"msg_type":2,"msg_type_str":"live_comment","sec_open_id":"viewer-comment","nickname":"观众"})";
    const std::string like = R"({"msg_id":"like-1","timestamp":1711939193044,"msg_type":1,"msg_type_str":"live_like","sec_open_id":"viewer-like","nickname":"点赞者"})";
    const std::string gift = R"({"msg_id":"gift-1","timestamp":1711939405000,"msg_type":3,"msg_type_str":"live_gift","sec_open_id":"viewer-gift","nickname":"送心人","sec_gift_id":"VQCVfwVeQCJZDCr/srE99db/NA3vgkBlaxG2dPyetE8NLgDRQ5ktGcwXm8M=","gift_num":2})";
    const std::string follow = R"({"msg_id":"follow-1","msg_type":5,"msg_type_str":"live_follow","sec_open_id":"viewer-follow","nickname":"关注者","user_follow_action":1})";
    const std::string enter = R"({"msg_id":"enter-1","msg_type":7,"msg_type_str":"live_enter_room","sec_open_id":"viewer-enter","nickname":"进房者"})";
    const auto result = douyin::MapOpenLiveData(Envelope(comment + "," + like + "," + gift + "," + follow + "," + enter), "session-1");
    Check(result.lines.size() == 5, "all supported events mapped");
    Check(result.lines[0].find("\"type\":\"comment\"") != std::string::npos, "comment type");
    Check(result.lines[0].find("\"display_name\":\"观众\"") != std::string::npos, "display name");
    Check(result.lines[0].find("\"timestamp_ms\":1711939362000") != std::string::npos, "timestamp");
    Check(result.lines[1].find("\"type\":\"like\"") != std::string::npos, "like type");
    Check(result.lines[2].find("\"type\":\"gift\"") != std::string::npos && result.lines[2].find("\"count\":2") != std::string::npos, "gift count");
    Check(result.lines[3].find("\"type\":\"follow\"") != std::string::npos, "follow type");
    Check(result.lines[4].find("\"type\":\"enter_room\"") != std::string::npos, "enter type");
    Check(douyin::SessionStartLine("session-1") == R"({"v":1,"type":"session_start","session_id":"session-1"})", "session start envelope");
    Check(douyin::SessionEndLine("session-1") == R"({"v":1,"type":"session_end","session_id":"session-1"})", "session end envelope");
}
void TestRejectionAndDedup() {
    const std::string invalid = R"({"msg_id":"bad","msg_type":2,"msg_type_str":"live_comment","sec_open_id":"v","nickname":"n")";
    const std::string unsupported = R"({"msg_id":"fan","msg_type":4,"msg_type_str":"live_fansclub","sec_open_id":"v","nickname":"n"})";
    Check(douyin::MapOpenLiveData(Envelope(invalid + "," + unsupported), "s").lines.empty(), "malformed unsupported ignored");
    douyin::MessageIdDeduplicator dedup(2);
    Check(dedup.Accept("a") && !dedup.Accept("a") && dedup.Accept("b") && dedup.Accept("c") && dedup.Accept("a"), "bounded duplicate eviction");
}
void TestExtractionAndTail() {
    const std::string callback = Envelope(R"({"msg_id":"x","msg_type":1,"msg_type_str":"live_like","sec_open_id":"v","nickname":"n"})");
    std::string line = "PipeCapture OPEN_LIVE_DATA " + callback;
    std::string extracted;
    Check(douyin::ExtractOpenLiveData(line, extracted) && extracted == callback, "capture extraction");
    Check(!douyin::ExtractOpenLiveData("PipeCapture OPEN_LIVE_DATA {broken", extracted), "broken capture rejected");
    Check(douyin::IsWebcastMateDisconnect("PipeCapture EVENT_DISCONNECTED"), "disconnect marker");

    const auto path = std::filesystem::temp_directory_path() / "douyin-live-gateway-tail.log";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    {
        std::ofstream out(path, std::ios::binary);
        out << line.substr(0, line.size() - 2);
    }
    douyin::LogTailer tailer(path);
    std::vector<std::string> records;
    bool rotated = false;
    Check(tailer.Poll(records, rotated) && records.empty(), "partial line buffered");
    { std::ofstream out(path, std::ios::binary | std::ios::app); out << line.substr(line.size() - 2) << '\n'; }
    Check(tailer.Poll(records, rotated) && records.size() == 1, "completed line emitted");
    const std::string replacement = "PipeCapture OPEN_LIVE_DATA " + Envelope(R"({"msg_id":"replacement","msg_type":1,"msg_type_str":"live_like","sec_open_id":"v","nickname":"n"})");
    { std::ofstream out(path, std::ios::binary | std::ios::trunc); out << replacement << '\n'; }
    Check(tailer.Poll(records, rotated) && rotated && records.size() == 1, "truncate rotation handled");
    std::filesystem::remove(path, ec);
}
} // namespace
int main() {
    TestArguments(); TestMappedEvents(); TestRejectionAndDedup(); TestExtractionAndTail();
    if (failures) return 1;
    std::cout << "All DouyinLiveGateway core tests passed\n";
    return 0;
}
