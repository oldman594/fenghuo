# RFD: Central service, device, app, and map architecture

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

The current Fenghuo AP prototype already covers the minimal room and battle loop. The next stage
should define the production-facing software architecture around that core: a central battle
service, player-worn devices, infrared guns, and a mobile app that shows rooms, live map state, and
battle status.

This RFD proposes that `fenghuo` continue evolving as the central service. It should not run as an
independent AP process on every player headset. The headset-side software should instead be a
lightweight device client or gateway that reports hardware and position data to the central service
and receives feedback commands.

## Motivation

The current prototype is good at one thing: maintaining one authoritative battle state. That is the
right direction. The next product stage needs:

- shared room visibility across all users
- shared live map state across all users
- centralized battle arbitration and scoring
- consistent player/device binding
- live updates to the mobile app

If the full AP logic runs separately on each headset, the system becomes fragmented. Each device
would only see part of the truth, and room state, map state, and score state would be hard to keep
consistent.

The next stage should therefore preserve one authoritative service and treat devices and the mobile
app as clients of that service.

## System architecture

### Proposed deployment model

The software system should be split into three main roles:

```text
1. Central service
   - room management
   - player/device binding
   - battle state
   - hit arbitration
   - map state
   - live app updates

2. Device client or gateway
   - runs on headset receiver or nearby embedded controller
   - collects hardware signals
   - reports shot/hit/position/heartbeat
   - receives feedback commands such as hit alert or battle end

3. Mobile app
   - lists rooms and modes
   - lets players join rooms
   - shows player and room status
   - shows live map
   - subscribes to live updates
```

### Authoritative state ownership

The central service should own:

```text
rooms
players
device bindings
battles
scores
player alive state
player position state
live event stream
```

Devices should own only local responsibilities:

```text
reading sensors
detecting trigger/fire
detecting local hit receive events
emitting device heartbeats
playing feedback
```

The mobile app should own only presentation and user commands:

```text
room browsing
room join/leave commands
player status display
map display
operator or player UI actions
```

### Recommended process boundary

Recommended first production boundary:

```text
fenghuo central service
  <- HTTP/WebSocket ->
device clients and mobile app
```

If headset hardware cannot speak HTTP directly, add a small gateway process between the embedded
hardware runtime and `fenghuo`.

## Device protocol

### Scope

The first device protocol should be deliberately narrow. It should let devices:

- announce presence
- bind to a room/player
- report fire
- report hit receive or hit result
- report position
- receive feedback commands

The existing battle event ingestion contract remains useful, but device communication needs a layer
above raw battle events because the system must also manage:

- online/offline state
- device identity
- binding lifecycle
- position stream
- acknowledgements

### Device identity

Each device should have a stable `device_id`.

Example:

```text
device-head-red-01
device-gun-red-01
```

The current `module_id` field can remain the room-side binding key for V1, but the next production
phase should model device identity explicitly.

### Minimum device messages

Recommended V1 device-to-service messages:

```text
device_hello
device_heartbeat
device_bound
shot
hit
position_updated
device_status
```

Example `device_hello`:

```json
{
  "message_type": "device_hello",
  "device_id": "device-head-red-01",
  "device_kind": "headset_receiver",
  "firmware_version": "0.1.0",
  "occurred_at_ms": 1730000000000
}
```

Example `device_heartbeat`:

```json
{
  "message_type": "device_heartbeat",
  "device_id": "device-head-red-01",
  "battery_percent": 87,
  "signal_strength": 91,
  "occurred_at_ms": 1730000005000
}
```

Example `position_updated`:

```json
{
  "message_type": "position_updated",
  "device_id": "device-head-red-01",
  "player_id": "p-red-01",
  "room_id": "room-001",
  "position": {
    "x": 12.4,
    "y": 8.1,
    "heading_deg": 270.0
  },
  "occurred_at_ms": 1730000005100
}
```

Example `shot`:

```json
{
  "schema_version": 0,
  "event_type": "shot",
  "battle_id": "battle-001",
  "event_id": "hw-shot-0001",
  "source_id": "device-gun-red-01",
  "sequence": 1001,
  "occurred_at_ms": 1730000005200,
  "payload": {
    "player_id": "p-red-01",
    "weapon_id": "rifle-01",
    "ammo_after": 29
  }
}
```

Example `hit`:

```json
{
  "schema_version": 0,
  "event_type": "hit",
  "battle_id": "battle-001",
  "event_id": "hw-hit-0001",
  "source_id": "device-gun-red-01",
  "sequence": 1002,
  "occurred_at_ms": 1730000005250,
  "payload": {
    "attacker_player_id": "p-red-01",
    "target_player_id": "p-blue-01",
    "weapon_id": "rifle-01",
    "damage": 10,
    "hit_zone": "torso"
  }
}
```

### Service-to-device messages

The central service should also be able to send commands to the headset-side client.

Recommended V1 service-to-device messages:

```text
player_hit_feedback
battle_started_feedback
battle_ended_feedback
device_bound_feedback
device_error
```

Example `player_hit_feedback`:

```json
{
  "message_type": "player_hit_feedback",
  "device_id": "device-head-blue-01",
  "player_id": "p-blue-01",
  "battle_id": "battle-001",
  "feedback": {
    "effect": "vibrate_and_buzzer",
    "duration_ms": 300
  },
  "occurred_at_ms": 1730000005260
}
```

### Reliability rules

The device protocol should keep the same idempotency rule already used by battle events:

```text
source_id + sequence identifies one logical event
```

Retry behavior:

```text
if delivery fails, retry with the same source_id and sequence
```

### Transport recommendation

For the first production step:

```text
HTTP for commands and event upload
WebSocket for live downstream updates
```

This fits the existing `fenghuo` transport model and keeps implementation risk lower than starting
with a new binary transport.

## App interface

### App responsibilities

The mobile app should interact with the central service as a room and battle client. It does not
need to know device protocol details beyond player and room status.

### App-facing API groups

Recommended app-facing API areas:

```text
room discovery
room membership
battle snapshots
live room updates
live map updates
player profile and session status
```

### Recommended HTTP endpoints

The current room endpoints are a good start, but the app layer needs a more complete room and map
surface.

Recommended next endpoints:

```text
GET  /api/v1/rooms
GET  /api/v1/rooms/{room_id}
POST /api/v1/rooms/{room_id}/join
POST /api/v1/rooms/{room_id}/leave
GET  /api/v1/rooms/{room_id}/map
GET  /api/v1/battles/{battle_id}
GET  /api/v1/players/{player_id}/status
```

Example room list response:

```json
{
  "rooms": [
    {
      "room_id": "room-001",
      "name": "Friday match",
      "mode": "team_deathmatch",
      "phase": "open",
      "player_count": 6,
      "max_players": 8
    }
  ]
}
```

Example room join request:

```json
{
  "player_id": "p-red-01",
  "display_name": "Red 01"
}
```

Example player status response:

```json
{
  "player_id": "p-red-01",
  "room_id": "room-001",
  "battle_id": "battle-001",
  "team_id": "red",
  "alive": true,
  "health": 80,
  "device_online": true
}
```

### Live subscriptions

The app should subscribe to live updates instead of polling for fast-changing state.

Recommended live topics:

```text
room_updated
battle_updated
map_updated
player_status_updated
```

These can share one WebSocket connection if message envelopes remain explicit.

## Map position model

### Why this is a new module

The current AP prototype has battle state, but it does not yet have a true map model. The live map
is the largest new software capability in the next stage.

### Position state

Recommended first player position fields:

```text
player_id
room_id
battle_id
x
y
heading_deg
velocity_mps
updated_at_ms
source_device_id
```

Example:

```json
{
  "player_id": "p-red-01",
  "room_id": "room-001",
  "battle_id": "battle-001",
  "x": 12.4,
  "y": 8.1,
  "heading_deg": 270.0,
  "velocity_mps": 1.2,
  "updated_at_ms": 1730000005100,
  "source_device_id": "device-head-red-01"
}
```

### Coordinate system

The first version should explicitly choose one simple coordinate model:

```text
2D local map coordinates
unit: meters
origin: per-map configured origin
heading: clockwise degrees, 0-360
```

The system should not mix:

- GPS coordinates for one map
- local room coordinates for another map
- image pixel coordinates in the app

Instead, the central service should own normalized world coordinates and let the app transform them
for display.

### Update model

Position updates should be treated as state refreshes, not battle-history events.

Recommended split:

```text
battle events: append-only, auditable, replayable
position updates: latest-state stream with optional short retention
```

This avoids polluting the battle event log with high-frequency movement updates.

### Map snapshot and live update flow

Recommended map flow:

```text
device reports position
-> service validates device binding and room/battle context
-> service updates latest player position state
-> service publishes map_updated
-> app redraws live map
```

### Initial validation rules

- reject position updates for unbound devices
- reject position updates for players not in the room
- reject position updates if the room is closed
- accept stale tolerance only within a configured time window
- keep the most recent update per player as authoritative

## Recommended implementation order

The next phase should be developed in this order:

1. Confirm `fenghuo` as the central service and freeze that deployment model in docs.
2. Define the device identity and binding model.
3. Implement device registration, heartbeat, and binding endpoints.
4. Keep using existing `shot` and `hit` battle event ingestion for gameplay.
5. Add service-to-device feedback delivery for hit and battle lifecycle notifications.
6. Add position state storage and map update broadcasting.
7. Add app-facing room list, room join, player status, and map snapshot endpoints.
8. Add a lightweight device simulator for headset and gun traffic.
9. Add an app-facing web prototype or mobile test client before full mobile implementation.
10. Run hardware and app integration against one authoritative central service.

## Module recommendations

### Immediate next module

The first module to implement should be:

```text
device identity, registration, and binding
```

Reason:

- hardware team needs a stable contract
- app team needs reliable player/device status
- map state should not proceed without trusted device-player binding

### Second module

The second module should be:

```text
position ingestion and map state
```

Reason:

- the live map is one of the core user-facing features
- it introduces a new state model not covered by current room/battle code

### Third module

The third module should be:

```text
app-facing room and live map API
```

Reason:

- it can reuse the room system already implemented
- it should sit on top of stable device binding and position state

## Non-goals for this stage

- final mobile UI design
- anti-cheat strategy
- advanced path replay or analytics
- distributed multi-AP synchronization
- high-frequency 3D mapping
- hardware firmware architecture details

## Acceptance criteria

This architecture stage is accepted when:

- `fenghuo` is documented and implemented as one central authoritative service
- one device can register and bind to one player
- one device can report heartbeat and position
- one device can submit `shot` and `hit`
- the service can send hit feedback to the correct headset-side client
- the app can list rooms and join one room
- the app can fetch a live map snapshot
- the app can receive live room, battle, and map updates

## Open questions

- Will the headset receiver and the gun appear as two separate devices or one merged player device?
- Does hit detection originate on the gun side, the headset side, or a gateway that correlates both?
- Should device-to-service communication be direct over Wi-Fi, or proxied through a phone or edge
  gateway?
- How frequently can devices report position without exhausting battery or network budget?
- Does the app operate as a player client, an operator client, or both in the same product?
