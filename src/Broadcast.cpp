#include "douyin/Broadcast.h"

#include <algorithm>
#include <atomic>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace douyin {
namespace {

std::string EscapeJson(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) return {};
            result += character;
        }
    }
    return result;
}

bool JsonStringField(std::string_view frame, std::string_view key, std::string& value)
{
    const std::string quoted_key = "\"" + std::string(key) + "\"";
    const std::size_t key_at = frame.find(quoted_key);
    if (key_at == std::string_view::npos) return false;
    std::size_t cursor = frame.find(':', key_at + quoted_key.size());
    if (cursor == std::string_view::npos) return false;
    ++cursor;
    while (cursor < frame.size() && (frame[cursor] == ' ' || frame[cursor] == '\t')) ++cursor;
    if (cursor >= frame.size() || frame[cursor] != '"') return false;
    ++cursor;
    value.clear();
    while (cursor < frame.size()) {
        const char character = frame[cursor++];
        if (character == '"') return true;
        if (character != '\\' || cursor >= frame.size()) {
            value += character;
            continue;
        }
        const char escaped = frame[cursor++];
        switch (escaped) {
        case '"': value += '"'; break;
        case '\\': value += '\\'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        default: return false;
        }
    }
    return false;
}

bool IsObject(std::string_view frame)
{
    return !frame.empty() && frame.front() == '{' && frame.back() == '}';
}

bool HasJsonString(std::string_view frame, std::string_view key, std::string_view expected)
{
    std::string value;
    return JsonStringField(frame, key, value) && value == expected;
}

} // namespace

std::string MakeClientHello(std::string_view client)
{
    return "{\"type\":\"hello\",\"protocol\":\"" + std::string(GatewayProtocol)
        + "\",\"client\":\"" + EscapeJson(client) + "\"}";
}

std::string MakeReadyFrame(std::string_view subscriber_id, std::size_t queue_capacity)
{
    return "{\"type\":\"ready\",\"protocol\":\"" + std::string(GatewayProtocol)
        + "\",\"subscriber_id\":\"" + EscapeJson(subscriber_id)
        + "\",\"queue_capacity\":" + std::to_string(queue_capacity) + "}";
}

std::string MakeErrorFrame(std::string_view code)
{
    return "{\"type\":\"error\",\"protocol\":\"" + std::string(GatewayProtocol)
        + "\",\"code\":\"" + EscapeJson(code) + "\"}";
}

bool ParseClientHello(std::string_view frame, ClientHello& hello, std::string& error)
{
    error.clear();
    hello.client.clear();
    if (frame.size() > 4096) { error = "hello_too_large"; return false; }
    if (!IsObject(frame)) { error = "hello_invalid"; return false; }
    if (!HasJsonString(frame, "type", "hello")) { error = "hello_type"; return false; }
    if (!HasJsonString(frame, "protocol", GatewayProtocol)) {
        error = "hello_protocol";
        return false;
    }
    if (!JsonStringField(frame, "client", hello.client) || hello.client.empty()
        || hello.client.size() > 128) {
        error = "hello_client";
        return false;
    }
    return true;
}

bool NdjsonFramer::Feed(std::string_view bytes, std::vector<std::string>& frames)
{
    if (!error_.empty()) return false;
    pending_.append(bytes.data(), bytes.size());
    std::size_t newline = 0;
    while ((newline = pending_.find('\n')) != std::string::npos) {
        if (newline > max_frame_bytes_) { error_ = "frame_too_large"; return false; }
        std::string frame = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        if (!frame.empty() && frame.back() == '\r') frame.pop_back();
        if (frame.empty()) { error_ = "empty_frame"; return false; }
        frames.push_back(std::move(frame));
    }
    if (pending_.size() > max_frame_bytes_) { error_ = "frame_too_large"; return false; }
    return true;
}

bool NdjsonFramer::Finish()
{
    if (!error_.empty()) return false;
    if (!pending_.empty()) { error_ = "partial_frame"; return false; }
    return true;
}

SubscriberQueue::SubscriberQueue(std::string id, std::size_t capacity)
    : id_(std::move(id)), capacity_(std::max<std::size_t>(1, capacity)) {}

bool SubscriberQueue::Push(std::string frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return false;
    if (frames_.size() >= capacity_) {
        ++dropped_;
        closed_ = true;
        close_reason_ = "slow_consumer";
        frames_.clear();
        changed_.notify_all();
        return false;
    }
    frames_.push_back(std::move(frame));
    changed_.notify_one();
    return true;
}

bool SubscriberQueue::TryPop(std::string& frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (frames_.empty()) return false;
    frame = std::move(frames_.front());
    frames_.pop_front();
    return true;
}

bool SubscriberQueue::WaitPop(std::string& frame, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock, timeout, [this] { return closed_ || !frames_.empty(); });
    if (frames_.empty()) return false;
    frame = std::move(frames_.front());
    frames_.pop_front();
    return true;
}

void SubscriberQueue::Close(std::string reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return;
    closed_ = true;
    close_reason_ = std::move(reason);
    changed_.notify_all();
}

std::size_t SubscriberQueue::Dropped() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

bool SubscriberQueue::IsClosed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

std::string SubscriberQueue::CloseReason() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return close_reason_;
}

BroadcastHub::BroadcastHub(std::size_t queue_capacity, std::size_t max_frame_bytes)
    : queue_capacity_(std::max<std::size_t>(1, queue_capacity)),
      max_frame_bytes_(std::max<std::size_t>(1, max_frame_bytes)) {}

std::shared_ptr<SubscriberQueue> BroadcastHub::Subscribe(std::string client)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return {};
    auto subscriber = std::make_shared<SubscriberQueue>("sub-" + std::to_string(next_id_++),
                                                        queue_capacity_);
    subscribers_.push_back(subscriber);
    if (!current_session_start_.empty()) subscriber->Push(current_session_start_);
    static_cast<void>(client);
    return subscriber;
}

void BroadcastHub::Unsubscribe(std::string_view subscriber_id, std::string reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& subscriber : subscribers_) {
        if (subscriber->Id() == subscriber_id) subscriber->Close(reason);
    }
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
        [](const auto& subscriber) { return subscriber->IsClosed(); }), subscribers_.end());
}

void BroadcastHub::Publish(std::string_view ndjson_frame)
{
    if (ndjson_frame.empty() || ndjson_frame.size() > max_frame_bytes_
        || ndjson_frame.find_first_of("\r\n") != std::string_view::npos) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++rejected_;
        return;
    }
    std::vector<std::shared_ptr<SubscriberQueue>> subscribers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) return;
        ++published_;
        if (ndjson_frame.find(R"("type":"session_start")") != std::string_view::npos) {
            current_session_start_ = std::string(ndjson_frame);
        } else if (ndjson_frame.find(R"("type":"session_end")") != std::string_view::npos) {
            current_session_start_.clear();
        }
        subscribers = subscribers_;
    }
    for (const auto& subscriber : subscribers) subscriber->Push(std::string(ndjson_frame));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
            [](const auto& subscriber) { return subscriber->IsClosed(); }), subscribers_.end());
    }
}

void BroadcastHub::Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_) return;
    shutdown_ = true;
    for (const auto& subscriber : subscribers_) subscriber->Close("shutdown");
}

std::size_t BroadcastHub::SubscriberCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return subscribers_.size();
}

std::size_t BroadcastHub::PublishedCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

std::size_t BroadcastHub::RejectedCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return rejected_;
}

struct NamedPipeServer::State {
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::thread accept_thread;
#ifdef _WIN32
    std::mutex sessions_mutex;
    std::vector<std::thread> sessions;
    HANDLE listening_handle = INVALID_HANDLE_VALUE;
#endif
};

#ifdef _WIN32
namespace {

bool ReadLine(HANDLE pipe, std::string& line)
{
    NdjsonFramer framer(4096);
    std::vector<std::string> frames;
    char buffer[512];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) || read == 0) return false;
        if (!framer.Feed(std::string_view(buffer, read), frames)) return false;
        if (!frames.empty()) { line = std::move(frames.front()); return true; }
    }
}

bool WriteLine(HANDLE pipe, std::string_view line)
{
    std::string bytes(line);
    bytes.push_back('\n');
    DWORD written = 0;
    return WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size();
}

} // namespace
#endif

NamedPipeServer::NamedPipeServer(BroadcastHub& hub, std::string pipe_name)
    : hub_(hub), pipe_name_(std::move(pipe_name)), state_(std::make_unique<State>()) {}

NamedPipeServer::~NamedPipeServer() { Stop(); }

bool NamedPipeServer::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_->running) return true;
#ifdef _WIN32
    state_->stop_requested = false;
    state_->running = true;
    state_->accept_thread = std::thread([this] {
        while (!state_->stop_requested) {
            HANDLE pipe = CreateNamedPipeA(pipe_name_.c_str(), PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
                256 * 1024, 4096, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                std::lock_guard<std::mutex> error_lock(mutex_);
                error_ = "CreateNamedPipeA:" + std::to_string(GetLastError());
                break;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (state_->stop_requested) {
                    CloseHandle(pipe);
                    break;
                }
                state_->listening_handle = pipe;
            }
            const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (!connected || state_->stop_requested) {
                CloseHandle(pipe);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (state_->listening_handle == pipe) {
                        state_->listening_handle = INVALID_HANDLE_VALUE;
                    }
                }
                continue;
            }
            {
                std::lock_guard<std::mutex> sessions_lock(state_->sessions_mutex);
                state_->sessions.emplace_back([this, pipe] {
                std::string line;
                ClientHello hello;
                std::string error;
                auto close = [&] {
                    FlushFileBuffers(pipe);
                    DisconnectNamedPipe(pipe);
                    CloseHandle(pipe);
                };
                if (!ReadLine(pipe, line) || !ParseClientHello(line, hello, error)) {
                    WriteLine(pipe, MakeErrorFrame(error.empty() ? "hello_invalid" : error));
                    close();
                    return;
                }
                auto subscriber = hub_.Subscribe(hello.client);
                if (!subscriber || !WriteLine(pipe, MakeReadyFrame(subscriber->Id(),
                                                                     subscriber->Capacity()))) {
                    if (subscriber) hub_.Unsubscribe(subscriber->Id(), "server_shutdown");
                    close();
                    return;
                }
                std::string frame;
                while (!state_->stop_requested && subscriber->WaitPop(frame,
                           std::chrono::milliseconds(250))) {
                    if (!WriteLine(pipe, frame)) {
                        hub_.Unsubscribe(subscriber->Id(), "client_disconnect");
                        close();
                        return;
                    }
                }
                if (!subscriber->IsClosed()) hub_.Unsubscribe(subscriber->Id(), "server_stop");
                close();
                });
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (state_->listening_handle == pipe) {
                    state_->listening_handle = INVALID_HANDLE_VALUE;
                }
            }
        }
        state_->running = false;
    });
    return true;
#else
    error_ = "named_pipe_unavailable_on_this_platform";
    return false;
#endif
}

void NamedPipeServer::Stop()
{
    if (!state_) return;
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_->stop_requested = true;
        if (state_->accept_thread.joinable()) {
            CancelSynchronousIo(state_->accept_thread.native_handle());
        }
    }
    hub_.Shutdown();
    if (state_->accept_thread.joinable()) state_->accept_thread.join();
    std::unique_lock<std::mutex> sessions_lock(state_->sessions_mutex);
    for (auto& session : state_->sessions) {
        if (session.joinable()) CancelSynchronousIo(session.native_handle());
    }
    auto sessions = std::move(state_->sessions);
    sessions_lock.unlock();
    for (auto& session : sessions) if (session.joinable()) session.join();
#else
    state_->stop_requested = true;
#endif
    state_->running = false;
}

bool NamedPipeServer::Running() const { return state_ && state_->running; }

} // namespace douyin
