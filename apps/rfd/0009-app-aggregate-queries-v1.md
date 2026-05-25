# RFD: App aggregate queries V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds a first `api/v1` read layer for the mobile app. The goal is not to replace the
existing `api/v0` room and battle command surface. The goal is to present room, map, and player
status data in app-oriented shapes that are easy for a phone client to consume.

## Scope

This step includes:

- `GET /api/v1/rooms`
- `GET /api/v1/rooms/{room_id}`
- `GET /api/v1/rooms/{room_id}/map`
- `GET /api/v1/players/{player_id}/status`
- `POST /api/v1/rooms/{room_id}/join`
- `POST /api/v1/rooms/{room_id}/leave`

This step does not include:

- app authentication
- pagination
- filtering and search

## Design

### Endpoint set

```text
GET /api/v1/rooms
GET /api/v1/rooms/{room_id}
GET /api/v1/rooms/{room_id}/map
GET /api/v1/players/{player_id}/status
POST /api/v1/rooms/{room_id}/join
POST /api/v1/rooms/{room_id}/leave
```

### Unified response direction

The app layer should expose flattened, consumer-friendly objects instead of the full internal room
snapshot shape.

### Room list response

Each room item should include:

```text
room_id
name
mode
phase
player_count
max_players
battle_id
team_summaries
```

### Room detail response

Room detail should include:

```text
room
players
devices
positions
```

### Map response

Map response should include:

```text
room_id
phase
positions
```

### Player status response

Player status should aggregate across room, device, battle, and position state when available.

Recommended fields:

```text
player_id
display_name
room_id
room_phase
battle_id
team_id
ready
alive
health
device_id
device_online
position
```

### Join and leave commands

`join` and `leave` should be thin app-facing wrappers over the existing room event model.

`POST /api/v1/rooms/{room_id}/join` request should accept:

```text
event_id
source_id
sequence
occurred_at_ms
player_id
display_name
team_id
module_id
```

`POST /api/v1/rooms/{room_id}/leave` request should accept:

```text
event_id
source_id
sequence
occurred_at_ms
player_id
```

Both commands should return the same room detail shape used by `GET /api/v1/rooms/{room_id}`.

## Validation

- server integration test for `GET /api/v1/rooms`
- server integration test for `GET /api/v1/rooms/{room_id}`
- server integration test for `GET /api/v1/rooms/{room_id}/map`
- server integration test for `GET /api/v1/players/{player_id}/status`
- server integration test for `POST /api/v1/rooms/{room_id}/join`
- server integration test for `POST /api/v1/rooms/{room_id}/leave`
- full `just test`
