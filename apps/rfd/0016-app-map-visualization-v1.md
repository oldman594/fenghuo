# RFD: App map visualization V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step upgrades the `/app` map panel from a plain position table to a real 2D debug map.

The goal is not a production game map renderer. It is a practical operator and mobile-debug view
that lets us inspect player distribution, heading, online state, and basic battle state at a
glance while continuing to use the current aggregate APIs and `map_updated` WebSocket messages.

## Scope

This step includes:

- a 2D map surface in `/app`
- scaled player markers derived from latest room position state
- heading visualization
- team color, online state, and health hints on markers or side list
- a compact side list paired with the visual map

This step does not include:

- real venue floor plans
- obstacle geometry
- interpolation or path history
- pinch zoom or gesture navigation
- battle replay

## Design

### Data source

The view continues to consume:

- `GET /api/v1/rooms/{room_id}/map`
- `GET /api/v1/players/{player_id}/status`
- `WS /api/v0/live -> map_updated`

No backend protocol changes are required.

### Rendering model

- compute current bounds from the latest position set
- add padding around the visible world
- project world coordinates to a fixed-aspect 2D surface
- render one marker per player
- rotate a heading indicator by `heading_deg`

### Visual state

Each marker should expose at least:

- team color
- player short label
- online/offline state
- alive/down state when battle data exists

The paired side list should expose:

- exact coordinates
- heading
- velocity
- health when available

## Validation

- manual verification in `/app`
- full `just test` to confirm no regression in packaged app assets or server behavior
