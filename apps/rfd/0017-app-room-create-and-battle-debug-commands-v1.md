# RFD: App room create and battle debug commands V1

- Status: Draft
- Owners: apps
- Created: 2026-05-29
- Updated: 2026-05-29

## Summary

This step closes two major gaps in `/app`:

- create a room without dropping to `curl` or `/console`
- manually submit battle `shot` and `hit` events for debug verification

The design stays app-facing and thin. It reuses current room creation and battle event submission
paths and returns the same public JSON shapes already consumed by `/app`.

## Scope

This step includes:

- `POST /api/v1/rooms`
- `POST /api/v1/battles/{battle_id}/shot`
- `POST /api/v1/battles/{battle_id}/hit`
- `/app` create-room controls
- `/app` shot/hit debug controls

This step does not include:

- role-based permissions
- room template presets beyond minimal defaults
- automatic combat simulation
- weapon configuration management

## Design

### Room create

Add:

```text
POST /api/v1/rooms
```

Request body follows the existing room-created payload shape. The server should:

- generate `room_id` when omitted
- default `room_code` to `room_id` when omitted
- return app room detail shape

### Battle debug commands

Add:

```text
POST /api/v1/battles/{battle_id}/shot
POST /api/v1/battles/{battle_id}/hit
```

Shot request body:

```text
event_id
source_id
sequence
occurred_at_ms
player_id
weapon_id
ammo_after
```

Hit request body:

```text
event_id
source_id
sequence
occurred_at_ms
attacker_player_id
target_player_id
weapon_id
damage
hit_zone
```

Both commands return the same public battle snapshot shape used by:

```text
GET /api/v1/battles/{battle_id}
```

### App page

`/app` should expose:

- a room creation form
- shot debug controls:
  - attacker player
  - weapon
  - ammo_after
- hit debug controls:
  - attacker player
  - target player
  - weapon
  - damage
  - hit zone

The player selectors should derive from the current selected room and current battle.

## Validation

- server integration test for `POST /api/v1/rooms`
- server integration test for `POST /api/v1/battles/{battle_id}/shot`
- server integration test for `POST /api/v1/battles/{battle_id}/hit`
- full `just test`
