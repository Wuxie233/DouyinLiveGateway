#include "douyin/Broadcast.h"

#include <cassert>
#include <chrono>
#include <string>
#include <vector>

namespace {
using namespace douyin;

void HandshakeIsStable()
{
    assert(GatewayPipeName == R"(\\.\pipe\DouyinLiveGateway.v1)");
    assert(MakeClientHello("BillsMustBePaidMod") ==
        "{\"type\":\"hello\",\"protocol\":\"douyin-live-gateway.v1\",\"client\":\"BillsMustBePaidMod\"}");
    assert(MakeReadyFrame("sub-7", 4) ==
        "{\"type\":\"ready\",\"protocol\":\"douyin-live-gateway.v1\",\"subscriber_id\":\"sub-7\",\"queue_capacity\":4}");
    assert(MakeErrorFrame("hello_protocol") ==
        "{\"type\":\"error\",\"protocol\":\"douyin-live-gateway.v1\",\"code\":\"hello_protocol\"}");
    ClientHello hello;
    std::string error;
    assert(ParseClientHello(MakeClientHello("game\"client"), hello, error));
    assert(hello.client == "game\"client");
    assert(!ParseClientHello("{\"type\":\"hello\",\"protocol\":\"v0\",\"client\":\"x\"}", hello, error));
    assert(error == "hello_protocol");
}

void FramerHandlesChunksAndBounds()
{
    NdjsonFramer framer(16);
    std::vector<std::string> frames;
    assert(framer.Feed("{\"a\":", frames));
    assert(framer.Feed("1}\n{\"b\":2}\r\n", frames));
    assert((frames == std::vector<std::string>{"{\"a\":1}", "{\"b\":2}"}));
    assert(framer.Finish());
    NdjsonFramer partial(16);
    assert(partial.Feed("{\"partial\"}", frames));
    assert(!partial.Finish() && partial.Error() == "partial_frame");
    NdjsonFramer oversized(4);
    assert(!oversized.Feed("12345", frames));
    assert(oversized.Error() == "frame_too_large");
}

void SubscribersHaveIndependentOrderedQueues()
{
    BroadcastHub hub(4);
    auto first = hub.Subscribe("first");
    auto second = hub.Subscribe("second");
    assert(first && second && first->Id() == "sub-1" && second->Id() == "sub-2");
    hub.Publish("{\"event\":1}");
    hub.Publish("{\"event\":2}");
    std::string frame;
    assert(first->TryPop(frame) && frame == "{\"event\":1}");
    assert(first->TryPop(frame) && frame == "{\"event\":2}");
    assert(second->TryPop(frame) && frame == "{\"event\":1}");
    assert(second->TryPop(frame) && frame == "{\"event\":2}");
    assert(hub.PublishedCount() == 2);
}

void SlowConsumerDoesNotBlockOthers()
{
    BroadcastHub hub(2);
    auto slow = hub.Subscribe("slow");
    auto healthy = hub.Subscribe("healthy");
    std::string frame;
    hub.Publish("one");
    hub.Publish("two");
    assert(healthy->TryPop(frame) && frame == "one");
    assert(healthy->TryPop(frame) && frame == "two");
    hub.Publish("three");
    assert(slow->IsClosed() && slow->CloseReason() == "slow_consumer");
    assert(slow->Dropped() == 1);
    assert(healthy->TryPop(frame) && frame == "three");
    assert(hub.SubscriberCount() == 1);
}

void DisconnectReconnectAndShutdown()
{
    BroadcastHub hub;
    auto old = hub.Subscribe("old");
    hub.Unsubscribe(old->Id(), "client_disconnect");
    assert(old->IsClosed() && old->CloseReason() == "client_disconnect");
    auto replacement = hub.Subscribe("replacement");
    assert(replacement && replacement->Id() == "sub-2");
    hub.Publish("event");
    std::string frame;
    assert(replacement->WaitPop(frame, std::chrono::milliseconds(1)) && frame == "event");
    hub.Shutdown();
    assert(replacement->IsClosed() && replacement->CloseReason() == "shutdown");
    assert(!hub.Subscribe("late"));
}

void LateSubscribersReceiveCurrentSessionOnly()
{
    BroadcastHub hub;
    auto first = hub.Subscribe("first");
    hub.Publish(R"({"v":1,"type":"session_start","session_id":"s"})");
    hub.Publish(R"({"v":1,"type":"comment","session_id":"s","event_id":"e"})");
    auto late = hub.Subscribe("late");
    std::string frame;
    assert(late->TryPop(frame) && frame.find(R"("type":"session_start")") != std::string::npos);
    assert(!late->TryPop(frame));
    hub.Publish(R"({"v":1,"type":"session_end","session_id":"s"})");
    auto after = hub.Subscribe("after");
    assert(!after->TryPop(frame));
    static_cast<void>(first);
}

void InvalidFramesAreRejected()
{
    BroadcastHub hub;
    auto subscriber = hub.Subscribe("consumer");
    hub.Publish("");
    hub.Publish("bad\nframe");
    assert(hub.RejectedCount() == 2);
    std::string frame;
    assert(!subscriber->TryPop(frame));
}

void PlatformGuardIsExplicit()
{
#ifndef _WIN32
    BroadcastHub hub;
    NamedPipeServer server(hub);
    assert(!server.Start());
    assert(server.Error() == "named_pipe_unavailable_on_this_platform");
    server.Stop();
#endif
}
} // namespace

int main()
{
    HandshakeIsStable();
    FramerHandlesChunksAndBounds();
    SubscribersHaveIndependentOrderedQueues();
    SlowConsumerDoesNotBlockOthers();
    DisconnectReconnectAndShutdown();
    LateSubscribersReceiveCurrentSessionOnly();
    InvalidFramesAreRejected();
    PlatformGuardIsExplicit();
    return 0;
}
