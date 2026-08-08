# DouyinLiveGateway

Credential-free local gateway for normalized Douyin Live Companion events.

The gateway reads `webcast_mate` text logs, extracts supported
`PipeCapture`/`OPEN_LIVE_DATA` envelopes, and broadcasts bounded NDJSON events
to local consumers. It is intended to be the single source-listening process
for game mods, chat tools, and other local interactive programs.

The gateway is an independent process and repository. It does not use PipeSDK,
store credentials or raw payloads, control Live Companion, or provide durable
replay. Live Companion remains the owner of its own connection and subscription
lifecycle.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows x64, configure with the Visual Studio generator and build the
`DouyinLiveGateway` target:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target DouyinLiveGateway
```

Run `DouyinLiveGateway.exe --logPath <webcast_mate-log>` to select a source
explicitly, or omit `--logPath` for bounded automatic discovery. The process
waits for a valid source instead of guessing an ambiguous file. It publishes
`session_start` before the first accepted event and one `session_end` on a
recognized disconnect before exiting.

## Broadcast protocol

The gateway creates the same-user Windows named pipe
`\\.\pipe\DouyinLiveGateway.v1`. A subscriber sends one LF-terminated hello:

```json
{"type":"hello","protocol":"douyin-live-gateway.v1","client":"your-consumer"}
```

The gateway replies with a `ready` frame containing a bounded queue capacity,
then sends one normalized UTF-8 NDJSON event per LF-terminated line. Supported
event types are `session_start`, `session_end`, `comment`, `like`, `follow`,
`enter_room`, and heart `gift`. Events contain `v: 1`, `session_id`,
`event_id`, `viewer_id`, and `display_name` where applicable; gifts also contain
the supported `gift_id` and positive `count`.

Delivery is realtime first: a reconnecting subscriber receives future events
only. A subscriber joining during an active session receives the current
`session_start` state frame, but no prior interaction events. Each subscriber
has an independent bounded FIFO; a slow consumer is closed with an error state
and cannot block other consumers. Malformed hello frames are rejected and the
connection is closed.

The core keeps only bounded callback data while parsing. It does not write raw
log lines, callback payloads, credentials, or cookies. Delivery is realtime
first: reconnecting consumers receive future events only, and deduplication is
in-memory with a bounded message-id window.
