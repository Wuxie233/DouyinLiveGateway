# DouyinLiveGateway Development

## Architecture

- The gateway is a credential-free Windows x64 C++17 process. It tails a Live Companion `webcast_mate` text log, extracts `PipeCapture` `OPEN_LIVE_DATA`, maps supported events, and broadcasts normalized UTF-8 NDJSON over a same-user named pipe.
- `src/LogBridge.cpp` owns bounded log discovery/tailing, partial lines, truncation/replacement recovery, disconnect markers, and in-memory message-id deduplication.
- `src/LiveDataMapper.cpp` owns the public normalized event contract. It must not expose raw payloads or private economic data.
- `src/Broadcast.cpp` and `include/douyin/Broadcast.h` own the local multi-subscriber transport, handshake, bounded per-subscriber queues, slow-consumer isolation, and Windows named-pipe lifecycle.
- `src/GatewayMain.cpp` is the Windows process entry point and composes the tailer, mapper, deduplicator, and broadcaster. It does not own consumer-specific behavior.

## Conventions

- Public protocol version is `douyin-live-gateway.v1`; named pipe is `DouyinLiveGateway.v1`.
- Frames are one JSON object per LF-terminated line. A subscriber sends `hello`, receives `ready`, then receives normalized events. Reconnect has no event replay; a late subscriber receives only the current `session_start` state frame.
- Keep queue and frame limits bounded. A slow subscriber is disconnected independently and must not block other consumers.
- Keep diagnostics bounded and categorical. Never persist credentials, cookies, raw log lines, or raw event payloads.

## Commands

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Windows x64 builds should use the Visual Studio generator and target `DouyinLiveGateway`. The runtime accepts `--logPath <file>` or bounded automatic discovery.

## Module Map

- `include/douyin/`: public C++ interfaces for arguments, JSON, mapping, log tailing, and broadcast transport.
- `src/`: implementation and Windows process entry point.
- `tests/`: deterministic Linux-compatible core and broadcast tests.
