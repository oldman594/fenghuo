# RFD: App position debug commands V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds an app-facing position write command and matching `/app` debug controls so mobile-side
position reporting can be exercised without falling back to `api/v0`.

The implementation stays as a thin wrapper over the existing room map event flow. Validation,
snapshot updates, replay, and live `map_updated` delivery remain owned by room runtime.

## Scope

This step includes:

- `POST /api/v1/rooms/{room_id}/positions`
- `/app` controls to select player and device and submit position updates
- integration coverage for the app-facing write path

This step does not include:

- movement history playback
- map rendering changes beyond the existing debug table
- device-side batching or offline upload

## Design

### Endpoint

```text
POST /api/v1/rooms/{room_id}/positions
```

### Request shape

```text
event_id
source_id
sequence
occurred_at_ms
player_id
source_device_id
x
y
heading_deg
velocity_mps
```

### Behavior

- translate the request to `room_player_position_updated`
- reuse current room-runtime validation:
  - player must exist in room
  - device must exist in room
  - device must be bound to the same player
  - room phase must allow map updates
- return the same public room-detail shape used by:

```text
GET /api/v1/rooms/{room_id}
```

### App page

The `/app` map panel should expose:

- player selector
- source device selector
- x input
- y input
- heading input
- velocity input
- submit button

On submit, the page posts to `POST /api/v1/rooms/{room_id}/positions` and then refreshes the
selected room state. Live `map_updated` messages continue to update the map view in place.

## Validation

- server integration test for `POST /api/v1/rooms/{room_id}/positions`
- verify returned room detail includes updated position state
- full `just test`
