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
