# RFD: App auto battle debug mode V1

- Status: Draft
- Owners: apps
- Created: 2026-05-29
- Updated: 2026-05-29

## Summary

This step adds a client-side auto battle debug mode to `/app` so operators can continuously drive
combat events without repeatedly clicking `shot` and `hit`.

The feature is intentionally implemented as an app-side orchestrator. It reuses the existing
app-facing battle debug commands and does not change battle runtime rules or HTTP contracts.

## Scope

This step includes:

- auto fire loop controls in `/app`
- single-attacker repeated fire
- red-vs-blue alternating exchange
- stop conditions based on battle phase and alive players
- UI status for active auto battle loop

This step does not include:

- backend-side scheduled combat
- deterministic replay scripts
- pathing, movement, or aim simulation
- configurable weapon catalogs

## Design

### Reused endpoints

The mode continues to use:

```text
POST /api/v1/battles/{battle_id}/shot
POST /api/v1/battles/{battle_id}/hit
GET  /api/v1/battles/{battle_id}
```

### Modes

- `single_burst`: one attacker repeatedly shoots a chosen target
- `exchange`: choose one alive red player and one alive blue player and alternate attacks

### Timing

- configurable interval in milliseconds
- one loop iteration sends:
  - one `shot`
  - one `hit`
- the next iteration waits until the previous requests complete

### Stop conditions

Stop automatically when:

- battle is no longer `active`
- selected attacker or target is no longer valid in `single_burst`
- either side has no alive players left in `exchange`
- the user presses stop

### UI

The battle panel should expose:

- mode selector
- interval input
- start auto battle button
- stop auto battle button
- running status text

## Validation

- manual validation in `/app`
- full `just test` to confirm no regression
