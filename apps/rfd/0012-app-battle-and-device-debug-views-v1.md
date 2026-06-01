# RFD: App battle and device debug views V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step extends the `/app` debug page so it can validate more of the future phone-client
workflow: battle state, device state, player state, and map state together in one place.

To keep the page on app-facing data shapes, this step also adds a minimal battle query endpoint to
`api/v1`.

## Scope

This step includes:

- `GET /api/v1/battles/{battle_id}`
- `/app` battle panel
- `/app` device panel
- `/app` richer room/player/map presentation

This step does not include:

- battle write commands in `api/v1`
- device write commands in `api/v1`
- authentication

## Design

### Battle query

Add:

```text
GET /api/v1/battles/{battle_id}
```

Response should expose a battle snapshot that is directly consumable by the debug page:

```text
ok
battle_id
phase
mode
players
teams
started_at_ms optional
ended_at_ms optional
```

This can reuse the existing public battle snapshot JSON shape.

### App debug page

The page should show:

- room list
- selected room summary
- player statuses
- device states
- map snapshot
- battle summary when the room has a linked battle

### Websocket updates

The page should continue to consume:

- `room_summary_updated`
- `room_detail_updated`
- `player_status_updated`
- `map_updated`
- `accepted_event`

The `accepted_event` message is acceptable for the battle panel because battle snapshot shape is
already public and stable enough for debugging.

## Validation

- server integration test for `GET /api/v1/battles/{battle_id}`
- server integration test for `/app` assets still returning 200
- full `just test`
