# RFD: App battle operator commands V1

- Status: Draft
- Owners: apps
- Created: 2026-05-25
- Updated: 2026-05-25

## Summary

This step adds minimal app-facing operator commands for battle control so the `/app` page can
pause, resume, and end an active battle without dropping to `api/v0`.

## Scope

This step includes:

- `POST /api/v1/battles/{battle_id}/pause`
- `POST /api/v1/battles/{battle_id}/resume`
- `POST /api/v1/battles/{battle_id}/end`
- `/app` buttons for those commands

This step does not include:

- role-based permissions
- battle creation through `api/v1`
- shot/hit debug commands in `api/v1`

## Design

### Endpoint set

```text
POST /api/v1/battles/{battle_id}/pause
POST /api/v1/battles/{battle_id}/resume
POST /api/v1/battles/{battle_id}/end
```

### Request shapes

Pause:

```text
event_id
source_id
sequence
occurred_at_ms
reason
```

Resume:

```text
event_id
source_id
sequence
occurred_at_ms
```

End:

```text
event_id
source_id
sequence
occurred_at_ms
reason
```

### Response shape

All commands return the public battle snapshot shape used by:

```text
GET /api/v1/battles/{battle_id}
```

### App page

The `/app` battle panel should expose:

- Pause
- Resume
- End

Button state should follow current battle phase.

## Validation

- server integration test for app battle pause
- server integration test for app battle resume
- server integration test for app battle end
- full `just test`
