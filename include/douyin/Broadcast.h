#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace douyin {

inline constexpr std::string_view GatewayProtocol = "douyin-live-gateway.v1";
inline constexpr std::string_view GatewayPipeName = R"(\\.\pipe\DouyinLiveGateway.v1)";

struct ClientHello {
    std::string client;
};

std::string MakeClientHello(std::string_view client);
std::string MakeReadyFrame(std::string_view subscriber_id, std::size_t queue_capacity);
std::string MakeErrorFrame(std::string_view code);
bool ParseClientHello(std::string_view frame, ClientHello& hello, std::string& error);

// Splits byte chunks at LF while bounding an incomplete or complete frame.
class NdjsonFramer {
public:
    explicit NdjsonFramer(std::size_t max_frame_bytes = 256 * 1024)
        : max_frame_bytes_(max_frame_bytes == 0 ? 1 : max_frame_bytes) {}

    bool Feed(std::string_view bytes, std::vector<std::string>& frames);
    bool Finish();
    const std::string& Error() const { return error_; }

private:
    std::size_t max_frame_bytes_;
    std::string pending_;
    std::string error_;
};

class SubscriberQueue {
public:
    SubscriberQueue(std::string id, std::size_t capacity);

    bool Push(std::string frame);
    bool TryPop(std::string& frame);
    bool WaitPop(std::string& frame, std::chrono::milliseconds timeout);
    void Close(std::string reason);

    const std::string& Id() const { return id_; }
    std::size_t Capacity() const { return capacity_; }
    std::size_t Dropped() const;
    bool IsClosed() const;
    std::string CloseReason() const;

private:
    std::string id_;
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> frames_;
    std::size_t dropped_ = 0;
    bool closed_ = false;
    std::string close_reason_;
};

class BroadcastHub {
public:
    explicit BroadcastHub(std::size_t queue_capacity = 128,
                          std::size_t max_frame_bytes = 256 * 1024);

    std::shared_ptr<SubscriberQueue> Subscribe(std::string client);
    void Unsubscribe(std::string_view subscriber_id,
                     std::string reason = "client_disconnect");
    void Publish(std::string_view ndjson_frame);
    void Shutdown();

    std::size_t SubscriberCount() const;
    std::size_t PublishedCount() const;
    std::size_t RejectedCount() const;
    std::size_t QueueCapacity() const { return queue_capacity_; }

private:
    const std::size_t queue_capacity_;
    const std::size_t max_frame_bytes_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<SubscriberQueue>> subscribers_;
    std::size_t next_id_ = 1;
    std::size_t published_ = 0;
    std::size_t rejected_ = 0;
    bool shutdown_ = false;
    std::string current_session_start_;
};

// Windows implementation uses a byte-mode duplex named pipe. The public API
// stays platform-neutral so protocol and queue tests can run on Linux.
class NamedPipeServer {
public:
    explicit NamedPipeServer(BroadcastHub& hub,
                             std::string pipe_name = std::string(GatewayPipeName));
    ~NamedPipeServer();

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    bool Start();
    void Stop();
    bool Running() const;
    const std::string& Error() const { return error_; }

private:
    struct State;
    BroadcastHub& hub_;
    std::string pipe_name_;
    std::unique_ptr<State> state_;
    mutable std::mutex mutex_;
    std::string error_;
};

} // namespace douyin
