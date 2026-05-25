# RFD: Device registration and binding V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds the first production-facing device module to `fenghuo`: device registration,
heartbeat tracking, room-scoped player binding, unbinding, and room snapshot queries for device
state.

The implementation should stay inside the existing room model and room runtime. Device state is part
of room state because bindings are room-scoped, operator-visible, and must remain consistent with
room membership and room lifecycle.

## Scope

This step includes:

- registering a device into a room
- updating device heartbeat and online status
- binding a device to a player in a room
- unbinding a device from a player in a room
- listing room device state
- exposing device state in room snapshots

This step does not include:

- position ingestion
- service-to-device feedback delivery
- mobile app-specific API surface
- device authentication
- persistent global device inventory outside room history

## Design

### Room-owned device state

Add a `devices` collection to room state.

Recommended device fields:

```text
device_id
device_kind
display_name
online
battery_percent
signal_strength
bound_player_id
last_seen_at_ms
registered_at_ms
```

### Room event types

Add these room events:

```text
room_device_registered
room_device_heartbeat_updated
room_device_bound
room_device_unbound
```

### HTTP endpoints

Add room-scoped endpoints:

```text
GET  /api/v0/rooms/{room_id}/devices
POST /api/v0/rooms/{room_id}/devices
POST /api/v0/rooms/{room_id}/devices/{device_id}/heartbeat
POST /api/v0/rooms/{room_id}/devices/{device_id}/bind
POST /api/v0/rooms/{room_id}/devices/{device_id}/unbind
```

### Validation rules

- devices can be registered only while room is open
- devices can be bound only while room is open
- binding target player must exist in room
- one device can bind to at most one player
- one player can bind to at most one device
- device heartbeat may update both open and active rooms
- active room cannot accept new device registration in V1

## Validation

- room domain tests for register, bind, rebind rejection, unbind, heartbeat
- room protocol tests for new device event payloads
- room runtime tests for replay and duplicates with device events
- server integration tests for new HTTP endpoints
- demo flow through `tools/demo/hardware_v0_sequence.sh`
- full `just test`
