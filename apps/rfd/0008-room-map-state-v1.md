# RFD: Room map state V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds the first map and position module to `fenghuo`. The implementation stores latest
player position state inside room snapshots and exposes room-scoped map endpoints for update and
query.

The design deliberately treats position as latest state, not append-only battle history. It still
uses room events and room runtime so validation, replay, WebSocket updates, and room lifecycle stay
consistent with the rest of the current system.

## Scope

This step includes:

- room-scoped player position state
- position update events
- room map snapshot endpoint
- WebSocket `map_updated` live updates
- validation against room membership and device binding
- room snapshot exposure of map state

This step does not include:

- app-specific map rendering
- battle playback or movement history analytics
- interpolation or smoothing
- GPS or outdoor coordinate systems
- service-to-device map feedback

## Design

### Room-owned map state

Add a `positions` collection to room state.

Recommended position fields:

```text
player_id
source_device_id
x
y
heading_deg
velocity_mps
updated_at_ms
```

### Room event type

Add:

```text
room_player_position_updated
```

### HTTP endpoints

Add:

```text
GET  /api/v0/rooms/{room_id}/map
POST /api/v0/rooms/{room_id}/positions
WS   /api/v0/live -> map_updated
```

### Validation rules

- only registered room players can have position state
- `source_device_id` must reference a registered device
- `source_device_id` must be bound to the same `player_id`
- room must be `open` or `active`
- coordinates must be finite JSON numbers
- `heading_deg` is normalized to the `[0, 360)` range
- `velocity_mps` must be non-negative

## Validation

- room domain tests for position updates and binding validation
- room protocol tests for new position payload
- room runtime replay coverage with position events
- server integration tests for map endpoints
- server integration tests for `map_updated` websocket messages
- full `just test`
