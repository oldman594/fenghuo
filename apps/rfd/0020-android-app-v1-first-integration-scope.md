# RFD: Android App V1 first integration scope

- Status: Draft
- Owners: apps
- Created: 2026-06-01
- Updated: 2026-06-01

## Summary

This step turns the initial Flutter skeleton into a usable first integration client for the current
`fenghuo` backend.

The goal is not feature parity with `/app`. The goal is a clean first mobile client structure with
typed models, a small service layer, app state, and pages that let us validate the `api/v1` flow
from a real Android phone.

## Scope

This step includes:

- typed Dart models for room, player status, map, battle, and live messages
- HTTP service layer for current `api/v1`
- WebSocket live message subscription
- shared app state object
- first mobile pages for:
  - room list
  - selected room detail
  - map snapshot
  - battle snapshot
  - settings / backend endpoint

This step does not include:

- all write commands from `/app`
- device registration UI
- auto battle UI
- final visual design
- local persistence

## Page structure

### Rooms

- backend connection summary
- room list
- selected room summary
- players in selected room

### Battle

- selected battle summary
- team scores
- player battle state list

### Map

- map phase
- position list
- light visual snapshot of latest coordinates

### Settings

- backend base URL
- reconnect WebSocket
- live event log

## Data model set

Recommended first model set:

- `RoomSummary`
- `RoomDetail`
- `RoomPlayer`
- `PlayerStatus`
- `RoomMapSnapshot`
- `PlayerPosition`
- `BattleSnapshot`
- `BattlePlayer`
- `LiveMessage`

## API scope

### Queries

```text
GET /api/v1/rooms
GET /api/v1/rooms/{room_id}
GET /api/v1/rooms/{room_id}/map
GET /api/v1/players/{player_id}/status
GET /api/v1/battles/{battle_id}
```

### Live updates

```text
WS /api/v0/live
```

React to:

- `room_summary_updated`
- `room_detail_updated`
- `player_status_updated`
- `map_updated`
- `accepted_event`

## Validation

- local Dart analysis once Flutter toolchain is available
- local widget/smoke tests
- debug APK build once Android SDK is available
