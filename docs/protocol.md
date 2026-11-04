# Remote protocol v1 (LAN edition, app/server 0.2.0+)

## Transport

- Remote: `ws://host:port`, one UTF-8 JSON object per WebSocket text message, maximum 16,384 bytes. No authentication, connection code, certificate, TLS, or pairing step.
- Every object has `"v":1` and a string `type`. Binary frames and malformed messages are rejected. Browser and native WebSocket clients use the same protocol.
- IPC: one compact UTF-8 JSON object per LF-terminated line. Windows pipe: `\\.\pipe\TVTDataBroadcastingRemote-v1`. The existing current-user Windows pipe ACL is unchanged.
- A lock serializes brokers for a given IPC name because Windows permits multiple servers on the same named pipe.
- The broker assigns a UUID to each registered agent connection. IDs change after reconnect; PIDs/labels are informational.
- Agent registration must finish within 5 seconds. Peers expire after 15 seconds without liveness. Broker pings and agent heartbeats keep idle connections alive.

## Connection and target list

After the WebSocket handshake, the server immediately sends `ready`, followed by `targets`. The client need not send anything to obtain the list.

```json
{"v":1,"type":"ready","serverTime":1790000000000}
{"v":1,"type":"targets","targets":[{"id":"<uuid>","name":"Living room","channel":"Station","pid":1234,"enabled":true,"mode":"bml","busy":false,"targetToken":"living-room","identityConflict":false}]}
```

`serverTime` is Unix epoch milliseconds. The app records it with a monotonic timer to expire input after 1.5 seconds without depending on the phone's clock. App pings detect stalled connections after about 6 seconds.

The target order is unspecified. Use IDs for current-session command routing and tokens for persistent identity; render labels as text. `busy` means a connection owns that target, possibly the current connection. Request another list with `{"v":1,"type":"list"}`.

## Operation ownership

```json
{"v":1,"type":"claim","target":"<uuid>"}
{"v":1,"type":"selection","status":"claimed","target":"<uuid>"}
```

Each target has one owner, identified by its WebSocket connection. Claim statuses: `claimed`, `busy`, `target_gone`, `identity_conflict`. A failed claim preserves the previous selection and returns its ID (or an empty string). A successful switch releases the previous target.

`release` yields `selection` status `released`. Agent disconnect yields `target_gone` and clears the selection. Client disconnect releases its claim. The server never carries ownership across connections. App 0.3.0+ can request a fresh claim after reconnect, for the remembered identity at the same URL, and only if it is available (0.3.x uses the exact session ID; 0.4.0+ uses a unique persistent token when present). It never substitutes another target by label or PID, takes an occupied target, or replays a key. Explicit release forgets the remembered target. This is routing arbitration, not authentication.

## Keys

```json
{"v":1,"type":"key","id":"<request-id>","sequence":1,"target":"<uuid>","key":"blue","action":"press","expires":1790000001500}
{"v":1,"type":"result","id":"<request-id>","status":"dispatched"}
```

- `id`: nonempty string, at most 64 UTF-16 code units. UUIDs are recommended.
- `sequence`: positive integer, strictly increasing per connection, maximum 2^53-1. Reset on a new connection. Invalid keys consume a valid sequence.
- Keys: `data`, `up`, `down`, `left`, `right`, `enter`, `back`, `blue`, `red`, `green`, `yellow`, strings `0` through `12`.
- Only `press` is supported. Direction holds repeat press messages rather than maintaining a keydown/keyup session.
- `expires`: integer Unix epoch milliseconds in server time, later than receipt and at most 5 seconds ahead. Expired input is not dispatched. The agent also checks the deadline before UI dispatch.
- Results: `dispatched`, `unavailable`, `rejected`, `not_owner`, `target_gone`, `invalid_key`, `invalid_sequence`, `expired`, `busy`, `timeout`.
- `dispatched` means accepted by the native command path, not acknowledged by the content. A timeout does not prove the key was never delivered; do not retry it.

## Agent messages

Register once per IPC connection. State updates use the same complete metadata with type `state`. Caller-supplied IDs are ignored.

```json
{"v":1,"type":"register","targetToken":"living-room","name":"Living room","channel":"Station","pid":1234,"enabled":true,"mode":"bml"}
{"v":1,"type":"registered","target":"<uuid>"}
{"v":1,"type":"heartbeat"}
```

The broker forwards a valid owned key with the earlier of the remote deadline and its 2-second timeout. IPC remains compatible with the existing native plugin bridge.

```json
{"v":1,"type":"key","request":"<broker-request-uuid>","key":"blue","action":"press","expires":1790000001500}
{"v":1,"type":"result","request":"<broker-request-uuid>","status":"dispatched"}
```

Only the recipient IPC connection may acknowledge a request. `dispatched`/`unavailable` are forwarded; other statuses become `rejected`. Unknown/late IDs are ignored. Responses go only to the originating remote connection.

## Limits and shutdown

- 16 agents, 16 remote sockets, 30 incoming remote messages per second per connection.
- 8 pending commands per client, 128 total. The 1-second tick expires commands after their deadline.
- 64 KiB maximum queued output per socket; the native SDK has 16-entry queues.
- IPC lines are buffered across partial reads. Malformed or oversized input disconnects its sender.
- SDK disconnects clear queues and change an internal generation. Old completions are ignored. The UI polls up to 4 commands per 50 ms without network waits.
- Backgrounding disconnects the app. With auto-connect enabled, startup/resume reconnects to the saved URL and can restore a unique available `targetToken` via a new claim, falling back to the exact target ID for legacy plugins. Manual disconnect is respected for the current app run. Keys are never restored or replayed.
- Input already handed to the plugin cannot always be cancelled by release/disconnect.

## Persistent routing identity (0.4.0)

`register` and `state` accept optional `targetToken` (empty for legacy plugins, otherwise 1-64 ASCII letters/digits, dot, underscore or hyphen, case-sensitive). Invalid values disconnect the agent; values are never truncated. Targets expose `targetToken` and `identityConflict`. This is public routing metadata, not authentication. Plugin INI key `RemoteTargetToken` persists across process restarts and is editable in plugin settings; an absent key is generated and saved once.

A client remembers the token with its broker URL. When the selected agent disappears, it waits for a matching token and issues a new claim to its new session ID. Automatic claims include `targetToken`; the broker checks that the requested session still has that token and exactly one registered agent has it, preventing list/claim races. Duplicate tokens return `selection.status=identity_conflict`. Manual ID-only claims remain possible. A token is never used as the command target: key requests and pending results remain tied to the current session ID. Busy targets are not taken, and inputs are never replayed.

## Controls: caption and volume (0.5.0)

Value-carrying operations use `control` instead of `key`. They share the `id`, `sequence`, `target`, `expires`, ownership and result rules of keys (including the per-connection sequence counter), and are never retried or replayed.

```json
{"v":1,"type":"control","id":"<request-id>","sequence":2,"target":"<uuid>","name":"volume","value":40,"expires":1790000001500}
{"v":1,"type":"result","id":"<request-id>","status":"dispatched"}
```

| `name` | `value` | Effect |
|---|---|---|
| `caption` | boolean | Show/hide captions (same as the plugin's caption toggle) |
| `mute` | boolean | TVTest mute |
| `volume` | integer 0-100 | TVTest volume (absolute) |

- Agents advertise supported controls and their current values in `register`/`state` as an optional `controls` object, e.g. `{"caption":false,"volume":40,"mute":false}`. A non-object `controls` disconnects the agent. The broker keeps only known names with valid values (unknown names are dropped for forward compatibility) and exposes them on each entry of `targets`. Targets without controls (older plugins) omit the field.
- The broker forwards a control only to an agent that advertises that name; otherwise it answers `unsupported`. Older agents therefore never receive an IPC `control` message. Invalid names or values return `invalid_control`.
- IPC: `{"v":1,"type":"control","request":"<broker-request-uuid>","name":"volume","value":40,"expires":...}`, acknowledged with the same `result` message as keys.
- The native bridge applies controls on the UI thread and refreshes state immediately afterwards, so remotes see the new value without waiting for the 1-second poll. Controls work while the data-broadcasting renderer is disabled.
- The app coalesces volume changes (at most one per 150 ms, last value wins) to stay within the 30 messages/second limit. Volume steps build on the last requested value for 1.5 seconds so held buttons advance even before the state update arrives.

## Channel list and tuning (0.6.0)

Agents that support tuning advertise the current channel as `controls.channel`:

```json
{"space":0,"index":-1,"serviceId":1032,"networkId":32736,"transportStreamId":32736}
```

`space` is the TVTest tuning space and `index` the position in its channel list; the current channel reports `index` -1 (and `space` -1 when unknown). Remotes match list entries by space and IDs.

A remote requests the list with the usual command fields (`id`, `sequence`, `target`, `expires`, ownership):

```json
{"v":1,"type":"channels","id":"<request-id>","sequence":3,"target":"<uuid>","expires":1790000001500}
{"v":1,"type":"channelPage","id":"<request-id>","space":0,"order":0,"spaceName":"地デジ(UHF)","channels":[
  {"space":0,"index":0,"remoteControlKey":1,"name":"NHK総合","serviceId":1024,"networkId":32736,"transportStreamId":32736,
   "program":{"name":"ニュース","start":1790000000000,"duration":1800}}]}
{"v":1,"type":"result","id":"<request-id>","status":"dispatched"}
```

- Each page belongs to one tuning space; a space may span several pages with the same `order`, which also gives the tab order. Spaces without channels are omitted. Pages stay well below the 16 KiB limit (at most 200 entries).
- `program` is the programme on air when the list was built (`start` in Unix epoch milliseconds, `duration` in seconds) and is omitted when EPG data is unavailable. Disabled channels are not listed.
- The broker forwards `channels` only to agents that advertise `controls.channel` (otherwise `unsupported`) and relays pages only to the requesting connection. IPC uses `{"type":"channels","request":...,"expires":...}`; the agent answers with `channelPage` messages carrying `request`, then `result`.
- Tuning is a `control` named `channel` whose value has exactly `space`, `index`, `serviceId`, `networkId` and `transportStreamId` (all non-negative integers). The plugin tunes only if the list entry at that position still has the same IDs, so a changed channel file never tunes a different service.
