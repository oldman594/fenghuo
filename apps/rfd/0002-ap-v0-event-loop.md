# RFD: AP V0 event loop

- Status: Draft
- Owners: fenghuo
- Created: 2026-05-19
- Updated: 2026-05-19

## Summary

AP V0 implements the first simulator-driven battle loop: HTTP POST submits AP event envelopes,
`src/protocol` validates them, `src/ap_runtime` deduplicates and sequences accepted events,
`src/core` applies battle rules, `src/storage` appends accepted events to JSONL, and `src/server`
broadcasts accepted event notifications and battle snapshots over WebSocket.

## Motivation

The AP side must become useful before real infrared module feedback is available. The first
implementation needs a narrow, testable loop that proves the project boundaries work without
committing to module transport, anti-cheat, clock synchronization, or production persistence.

Without a written V0 loop, implementation can easily collapse into a monolithic server where JSON
parsing, scoring, file writes, and live broadcast all happen in the same handler. That would make
the system hard to test and hard to replace once module hardware arrives.

## Guide-level explanation

V0 treats the AP as an event processor.

```text
HTTP POST /api/v0/events
  -> parse JSON envelope
  -> validate schema and payload shape
  -> reject duplicates
  -> update battle state
  -> append accepted event to JSONL
  -> publish WebSocket update
```

The first working demo should be able to run without physical modules:

1. Start `fenghuo-apd`.
2. Connect an operator UI or test client to the WebSocket live endpoint.
3. POST `player_joined` events.
4. POST `battle_started`.
5. POST `shot` and `hit` events.
6. Observe score and health changes in the live snapshot stream.
7. POST `battle_ended`.
8. Inspect the JSONL accepted event log.

## Reference-level explanation

### Module responsibilities

`apps/apd` owns process concerns only:

- CLI arguments
- config loading
- logging
- signal handling
- process exit status

`src/server` owns network transport:

- HTTP POST endpoint for event ingress
- WebSocket endpoint for live updates
- conversion between HTTP/WebSocket framing and runtime calls
- transport-level errors and response codes

`src/protocol` owns external event shape:

- JSON event envelope DTOs
- schema version validation
- event type validation
- payload field validation
- conversion from valid protocol DTOs into runtime commands

`src/ap_runtime` owns AP orchestration:

- battle session registry
- accepted event deduplication by `event_id`
- sender-local sequence diagnostics by `source_id`
- ordering policy for accepted events
- storage-before-broadcast acceptance rule
- live subscriber fanout abstraction

`src/core` owns deterministic battle rules:

- player/team registration
- lifecycle transitions
- shot counters
- hit damage
- health/alive state
- score updates
- battle snapshot construction

`src/storage` owns durable accepted records:

- JSONL append writer
- event log file naming
- flush policy
- later replay API

`src/simulator` owns generated inputs:

- repeatable event sequences for demos and tests
- no bypass around `src/protocol`

### Event sequence

V0 accepts this normal sequence:

```text
player_joined*
  -> battle_started
  -> (shot | hit | player_state)*
  -> battle_ended
```

Rules:

- `player_joined` is allowed before `battle_started`.
- `battle_started` freezes the initial roster for V0. Late joins are rejected unless a later RFD
  accepts join-in-progress behavior.
- `shot`, `hit`, and `player_state` require an active battle.
- `battle_ended` is idempotent only for a byte-identical duplicate event; otherwise repeated end
  attempts are rejected.
- Events after `battle_ended` are rejected except exact duplicates already accepted.

### Acceptance order

For each POSTed event:

1. Server receives bytes and attaches AP receive time for diagnostics.
2. Protocol parses JSON and validates the envelope.
3. AP runtime checks duplicate `event_id`.
4. AP runtime checks battle lifecycle preconditions.
5. Core applies the event to an in-memory battle state copy or transactional mutation context.
6. Storage appends the accepted event to JSONL and flushes according to the configured policy.
7. AP runtime commits the in-memory state update.
8. AP runtime emits accepted event and current snapshot to live subscribers.
9. Server returns an accepted response.

The storage step intentionally happens before broadcast. If persistence fails, clients must not see
an event as accepted.

### Failure semantics

Protocol failures:

- malformed JSON -> HTTP 400
- unsupported `schema_version` -> HTTP 400
- missing required envelope field -> HTTP 400
- unknown `event_type` -> HTTP 400
- payload shape mismatch -> HTTP 400

Runtime rule failures:

- unknown `battle_id` for runtime-controlled event -> HTTP 404 or domain error response
- duplicate accepted `event_id` with identical bytes -> HTTP 200 with duplicate status and no state
  change
- duplicate `event_id` with different bytes -> HTTP 409
- stale or repeated `sequence` from same `source_id` -> accepted only if exact duplicate policy
  applies; otherwise HTTP 409
- event not allowed in current lifecycle state -> HTTP 409
- hit with unknown attacker or target -> HTTP 409

Storage failures:

- JSONL append/open/flush failure -> HTTP 500
- state is not committed
- WebSocket broadcast is not sent

WebSocket failures:

- one disconnected client is removed from the subscriber list
- disconnected clients do not affect accepted event processing
- a slow client may be dropped if it exceeds the configured backpressure buffer

Process failures:

- fatal startup errors exit nonzero
- runtime should prefer rejecting one bad event over terminating the process

### V0 snapshot

The V0 battle snapshot should include:

- `battle_id`
- lifecycle state
- mode
- started/ended timestamps when present
- players keyed by `player_id`
- team scores
- player health/alive state
- shot count
- hit count
- latest accepted event id

### JSONL record

Each accepted JSONL line stores:

- original event envelope
- AP receive time
- AP accepted time
- acceptance sequence number
- optional derived summary fields for debugging

The JSONL file is the accepted audit trail. Raw rejected events may be logged separately later, but
they are not part of the accepted event log.

## Drawbacks

- V0 rejects late joins, which may be too strict for some game modes.
- JSONL is easy to inspect but does not solve historical search or leaderboard queries.
- WebSocket requires connection management earlier than SSE would.
- Storage-before-broadcast increases per-event latency compared with optimistic broadcast.

## Rationale and alternatives

HTTP POST plus WebSocket wins because command/event ingress stays simple while the live channel can
grow into bidirectional operator control. JSONL wins for V0 because replay and debugging are more
important than indexed queries.

Starting with server handlers first was rejected. Core and runtime behavior must be testable without
network transport.

Broadcast-before-storage was rejected because clients would observe events that might not exist in
the durable audit trail.

## Prior art

- `embedev` scheduler pattern: thin process entrypoint, orchestration layer, and focused processors.
- Event-sourced systems that treat accepted events as the durable audit trail.
- Game server live-state loops that separate transport, validation, rules, persistence, and fanout.

## Unresolved questions

- Exact HTTP response schema for accepted, duplicate, rejected, and failed events.
- Exact WebSocket message schema for accepted event and snapshot updates.
- Whether JSONL should use one file per battle or one rolling AP-wide log.
- Flush policy: every accepted event, time-batched fsync, or configurable.
- Initial score rule for `hit`: fixed one point per hit, damage-based score, or kill-only score.

## Validation and rollout

- Unit:
  - protocol accepts valid envelopes and rejects malformed/unsupported payloads
  - core applies player join/start/shot/hit/end transitions deterministically
  - runtime rejects duplicate or lifecycle-invalid events without state mutation
  - storage writes one valid JSON object per accepted event
- Module-local integration:
  - runtime fake store and fake broadcaster verify storage-before-broadcast order
  - JSONL store appends multiple events and can read them back when replay is introduced
- Cross-module integration:
  - HTTP POST simulated event sequence produces expected WebSocket snapshots
  - `just test` inside the container
- Migration or rollout:
  - implement core/protocol/storage/runtime first
  - add server endpoints after runtime tests pass
  - keep simulator-only module input until a module transport RFD exists
- Deferred gaps:
  - physical module feedback
  - authentication
  - anti-cheat
  - clock synchronization
  - delivery acknowledgements
  - SQLite or indexed historical queries

## Decision log

- 2026-05-19: Draft created for the AP V0 event loop.

## Future possibilities

- Add `diag/` state and sequence facts after the loop is implemented.
- Add replay tool that rebuilds snapshots from JSONL.
- Add module gateway transport after hardware feedback protocol is accepted.
- Add SQLite projection generated from accepted JSONL events.
