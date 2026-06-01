# RFD: App write commands and debug page V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step extends the `api/v1` app-facing layer from read-only queries plus join/leave into a
minimal app command surface, and adds a lightweight debug page that consumes `api/v1` plus the
existing websocket aggregate messages.

The purpose is to validate the mobile-app contract before a real phone app exists.

## Scope

This step includes:

- `POST /api/v1/rooms/{room_id}/players/{player_id}/ready`
- `POST /api/v1/rooms/{room_id}/start`
- `POST /api/v1/rooms/{room_id}/close`
- static app debug page at `/app`
- app debug page reads `api/v1`
- app debug page subscribes to `WS /api/v0/live`

This step does not include:

- authentication
- app user identity
- native mobile app packaging
- operator-only device management screens

## Design

### App command endpoints

The `api/v1` command surface should stay thin and continue to translate to the existing room event
model.

Added endpoint set:

```text
POST /api/v1/rooms/{room_id}/players/{player_id}/ready
POST /api/v1/rooms/{room_id}/start
POST /api/v1/rooms/{room_id}/close
```

#### Ready command

Request:

```text
event_id
source_id
sequence
occurred_at_ms
ready
```

Behavior:

- maps to `room_player_ready_changed`
- returns the same room detail shape as `GET /api/v1/rooms/{room_id}`

#### Start command

Request:

```text
event_id
source_id
sequence
occurred_at_ms
battle_id optional
duration_ms optional
```

Behavior:

- maps to `room_started`
- reuses existing room-to-battle bootstrap behavior
- returns app room detail plus `battle_snapshot` when available

#### Close command

Request:

```text
event_id
source_id
sequence
occurred_at_ms
```

Behavior:

- maps to `room_closed`
- returns the same room detail shape as `GET /api/v1/rooms/{room_id}`

### App debug page

Add a dedicated page:

```text
GET /app
GET /app/styles.css
GET /app/app.js
```

The page should be a minimal operations/debug surface for validating the app contract.

#### Data sources

Use only:

- `GET /api/v1/rooms`
- `GET /api/v1/rooms/{room_id}`
- `GET /api/v1/rooms/{room_id}/map`
- `GET /api/v1/players/{player_id}/status`
- `POST /api/v1/...` write commands
- `WS /api/v0/live`

#### Websocket consumption

The page should consume:

- `room_summary_updated`
- `room_detail_updated`
- `player_status_updated`
- `map_updated`

The page should not depend on `room_updated` internal snapshot shape.

#### Page capabilities

Minimal workflow:

- fetch room list
- select a room
- inspect room detail
- join a room
- set player ready state
- start room
- close room
- inspect live map snapshot
- inspect live player status

### Validation

- server integration test for `POST /api/v1/rooms/{room_id}/players/{player_id}/ready`
- server integration test for `POST /api/v1/rooms/{room_id}/start`
- server integration test for `POST /api/v1/rooms/{room_id}/close`
- server integration test for `/app`, `/app/styles.css`, `/app/app.js`
- full `just test`
