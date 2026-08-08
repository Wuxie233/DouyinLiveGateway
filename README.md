# DouyinLiveGateway

Credential-free local gateway for normalized Douyin Live Companion events.

The gateway reads `webcast_mate` text logs, extracts supported
`PipeCapture`/`OPEN_LIVE_DATA` envelopes, and broadcasts bounded NDJSON events
to local consumers. It is intended to be the single source-listening process
for game mods, chat tools, and other local interactive programs.

The implementation and protocol are being extracted from
`BillsMustBePaidMod/src/DouyinLiveAdapter/`. The gateway does not use PipeSDK,
store credentials or raw payloads, control Live Companion, or provide durable
replay.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows x64, configure with the Visual Studio generator and build the
`DouyinLiveGateway` target. Run it with `DouyinLiveGateway.exe --logPath
<webcast_mate-log>`. Each normalized event is one UTF-8 NDJSON object with
`v: 1`, `type`, `session_id`, `event_id`, `viewer_id`, and `display_name` where
applicable. Supported types are `session_start`, `session_end`, `comment`,
`like`, `follow`, `enter_room`, and heart `gift`.

The core keeps only bounded callback data while parsing. It does not write raw
log lines, callback payloads, credentials, or cookies. Delivery is realtime
first: reconnecting consumers receive future events only, and deduplication is
in-memory with a bounded message-id window.
