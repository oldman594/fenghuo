# RFD: App device debug commands V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds minimal app-facing device debug commands so the `/app` page can validate headset
registration, binding, unbinding, and heartbeat updates without falling back to `api/v0`.

## Scope

This step includes:

- `POST /api/v1/rooms/{room_id}/devices`
- `POST /api/v1/rooms/{room_id}/devices/{device_id}/bind`
- `POST /api/v1/rooms/{room_id}/devices/{device_id}/unbind`
- `POST /api/v1/rooms/{room_id}/devices/{device_id}/heartbeat`
- `/app` controls for those commands

This step does not include:

- automatic device discovery
- authentication
- device provisioning lifecycle beyond room-local debug actions

## Design

### Endpoints

```text
POST /api/v1/rooms/{room_id}/devices
POST /api/v1/rooms/{room_id}/devices/{device_id}/bind
POST /api/v1/rooms/{room_id}/devices/{device_id}/unbind
POST /api/v1/rooms/{room_id}/devices/{device_id}/heartbeat
```

These should stay thin wrappers over the existing room event model and return the same room detail
shape as `GET /api/v1/rooms/{room_id}`.

### App page controls

The `/app` page should provide:

- device registration form
- bind action from a device to an in-room player
- unbind action
- heartbeat action that can toggle online state and battery/signal values

## Validation

- server integration test for app device register
- server integration test for app device bind
- server integration test for app device heartbeat
- server integration test for app device unbind
- full `just test`
