# RFD: App websocket aggregate updates V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds app-facing websocket aggregate messages on the existing `WS /api/v0/live`
channel. The goal is to let a phone client react to room, player, and map changes without
re-querying every `api/v1` endpoint after each event.

These messages are additive. Existing `accepted_event`, `room_updated`, and `map_updated`
messages remain unchanged.

## Scope

This step includes:

- `room_summary_updated`
- `room_detail_updated`
- `player_status_updated`
- reuse of the current `map_updated`
- alignment of websocket aggregate payloads with `api/v1` response shapes

This step does not include:

- websocket authentication
- channel subscription and filtering
- a separate `/api/v1/live` websocket endpoint

## Design

### Transport

All aggregate messages continue to use:

```text
WS /api/v0/live
```

This keeps the transport surface stable while allowing the app to consume richer, higher-level
payloads.

### Message types

#### `room_summary_updated`

Published when a room event changes room list visible state.

Payload shape:

```text
type
event
room
```

`room` uses the same shape as `GET /api/v1/rooms` items.

#### `room_detail_updated`

Published when a room event changes room detail visible state.

Payload shape:

```text
type
event
room
players
devices
positions
```

These fields use the same shape as `GET /api/v1/rooms/{room_id}`.

#### `player_status_updated`

Published when a room or battle event changes player-facing status.

Payload shape:

```text
type
event
player_id
status
```

`status` uses the same shape as `GET /api/v1/players/{player_id}/status`.

If a player leaves the room and no current status can be built from room state anymore, the server
publishes:

```text
status = null
```

This allows the app to clear cached player state.

### Trigger rules

#### Room-driven triggers

Every accepted room event should continue to publish `room_updated`.

In addition:

- every room event publishes `room_summary_updated`
- every room event publishes `room_detail_updated`
- `room_player_position_updated` continues to publish `map_updated`
- player-affecting room events publish `player_status_updated`

Player-affecting room events include:

- `room_player_joined`
- `room_player_left`
- `room_player_team_changed`
- `room_player_ready_changed`
- `room_device_registered` when the device is already bound
- `room_device_heartbeat_updated` when the device is bound
- `room_device_bound`
- `room_device_unbound`
- `room_player_position_updated`
- `room_started`
- `room_ended`
- `room_closed`

#### Battle-driven triggers

Every accepted battle event should continue to publish `accepted_event`.

In addition, if the battle can be mapped back to a room snapshot, battle events may publish
`player_status_updated` for affected players:

- `player_joined`
- `battle_started`
- `hit`
- `player_state_updated`
- `battle_paused`
- `battle_resumed`
- `battle_ended`

The first implementation only needs to publish messages when the public player status shape
actually changes or can be recomputed safely from current room and battle state.

## Validation

- server integration test for room-driven `player_status_updated`
- server integration test for battle-driven `player_status_updated`
- server integration test for `room_summary_updated`
- server integration test for `room_detail_updated`
- full `just test`
