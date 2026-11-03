# Remote protocol v1

## Transport and identity

- Remote: one UTF-8 JSON object per WebSocket text message, maximum 16,384 bytes. Binary frames and nonempty browser Origin headers are rejected. Every object has `"v":1` and a string `type`.
- IPC: one compact UTF-8 JSON object per LF-terminated line, same size limit. Windows named pipe: `\\.\pipe\TVTDataBroadcastingRemote-v1`. Only the current Windows user is granted access. Pipe names can be overridden consistently on both sides.
- One broker owns a given IPC name. The broker explicitly locks that name because Windows permits multiple servers on a named pipe.
- The broker assigns an unpredictable UUID to each registered agent connection. IDs change after reconnect and are never inferred from a PID. Labels/PIDs are informational.
- Remote authentication must finish within 5 seconds. Agent registration must finish within 5 seconds. Registered peers expire after 15 seconds without liveness. Broker WebSocket pings and agent heartbeats maintain liveness.
- LAN requires WSS (TLS 1.2+). A manually confirmed SHA-256 leaf-certificate pin replaces CA/hostname trust for a self-signed server; certificate validity dates are still checked. Without a pin, platform CA and hostname verification apply. WS is allowed only on loopback.

## Remote messages

Authenticate first. The token is random and at least 32 UTF-8 bytes; recommended server-generated format is 64 hex characters. Never log the auth object.

```json
{"v":1,"type":"auth","token":"<secret>"}
```

Success yields `authenticated`, then `targets`. Authentication failure closes the connection with policy-violation code 1008. An authenticated client may request another list with `{"v":1,"type":"list"}`.

`authenticated` includes `serverTime` (Unix epoch milliseconds). The app records this with a monotonic elapsed timer and uses the estimated server time to expire each input after 1.5 seconds, without relying on the phone's wall clock. App-initiated pings detect a stalled connection after about 6 seconds.

```json
{"v":1,"type":"targets","targets":[{"id":"<uuid>","name":"Living room","channel":"Station","pid":1234,"enabled":true,"mode":"bml","busy":false}]}
```

The order of targets is unspecified. Render labels as text. Use the ID as the identity. A `busy` target is owned by some connection, possibly the current one.

```json
{"v":1,"type":"claim","target":"<uuid>"}
{"v":1,"type":"selection","status":"claimed","target":"<uuid>"}
```

Claims are exclusive per target and belong to a WebSocket connection. Claim statuses: `claimed`, `busy`, `target_gone`. A failed claim preserves the previous selection and returns its ID (or an empty string). A successful switch releases the previous target. `release` yields selection status `released`. Agent disconnect yields `target_gone` and clears the selection. Client disconnect releases its claim. Reconnection never restores a claim automatically.

```json
{"v":1,"type":"key","id":"<request-id>","sequence":1,"target":"<uuid>","key":"blue","action":"press","expires":1790000001500}
{"v":1,"type":"result","id":"<request-id>","status":"dispatched"}
```

- `id`: nonempty string, at most 64 UTF-16 code units. Clients should use UUIDs.
- `sequence`: positive integer, strictly increasing per authenticated WebSocket connection, maximum 2^53-1. Reset on a new connection. Invalid keys also consume a valid sequence number.
- Keys: `data`, `up`, `down`, `left`, `right`, `enter`, `back`, `blue`, `red`, `green`, `yellow`, strings `0` through `12`.
- Only `press` is supported. Direction-key holds are bounded repetitions of press messages, not physical keydown/keyup sessions.
- `expires`: required integer Unix epoch milliseconds in server time, later than receipt time and at most 5 seconds ahead. Expired inputs are never dispatched, including inputs delayed in TCP buffers. The native agent also checks the deadline immediately before UI dispatch. The broker forwards the earlier of this deadline and its 2-second timeout.
- Results: `dispatched`, `unavailable`, `rejected`, `not_owner`, `target_gone`, `invalid_key`, `invalid_sequence`, `expired`, `busy`, `timeout`.
- `dispatched` means accepted by the existing native command path, not a content-level acknowledgement. A timeout does not prove the key was never delivered. **Do not retry timed-out commands.**

## Agent messages

Register once per IPC connection. State updates supply the same complete metadata with type `state`. The broker ignores any caller-supplied identity and uses its own connection ID.

```json
{"v":1,"type":"register","name":"Living room","channel":"Station","pid":1234,"enabled":true,"mode":"bml"}
{"v":1,"type":"registered","target":"<uuid>"}
{"v":1,"type":"heartbeat"}
```

The broker translates a valid owned key into an IPC request. `expires` is Unix epoch milliseconds on the same PC. The SDK drops expired requests before dispatch.

```json
{"v":1,"type":"key","request":"<broker-request-uuid>","key":"blue","action":"press","expires":1790000002000}
{"v":1,"type":"result","request":"<broker-request-uuid>","status":"dispatched"}
```

Only the IPC connection to which the request was sent may acknowledge it. Agent results accept `dispatched`/`unavailable`; other status values become `rejected`. Unknown and late request IDs are ignored. The broker forwards responses only to the originating remote connection.

## Limits and shutdown

- 16 agents, 16 remote sockets; no more than 30 incoming remote messages per second per connection.
- At most 8 pending commands per client, 128 total. Commands time out after 2 seconds (checked on the 1-second broker tick).
- At most 64 KiB of queued outbound data per socket before disconnect; native SDK has 16-entry command/result queues.
- IPC lines are buffered across partial reads. Malformed or oversized messages disconnect their sender.
- Native SDK disconnects clear all unprocessed command/result queues and change an internal generation. UI completion from an old generation is ignored. The UI polls up to 4 commands per 50 ms tick and never waits on network I/O.
- App backgrounding disconnects rather than retaining an operation lease. No selection or key is restored after reconnect.
- Input already handed to the plugin cannot always be cancelled by release/disconnect. A routing acknowledgement does not guarantee what a broadcast application does with the key.
