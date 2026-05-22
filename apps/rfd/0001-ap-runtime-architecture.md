# RFD: AP runtime architecture

- Status: Draft
- Owners: fenghuo
- Created: 2026-05-19
- Updated: 2026-05-19

## Summary

Fenghuo starts with an AP-side runtime that accepts simulator or future module events, validates
them through a protocol boundary, applies battle rules, records an append-only JSONL event log, and
exposes live battle snapshots to operator UI clients over WebSocket. Module firmware and real
infrared feedback transport remain out of scope for this RFD.

## Motivation

The project needs a stable development process before AP implementation begins. The AP must record
battle information in real time, but the module-side hit feedback contract is not ready. If AP code
starts directly from UI or transport details, the project will likely mix game rules, protocol
parsing, persistence, and live delivery in one service.

The first decision is therefore to define AP-side ownership boundaries and a simulator-friendly event
protocol so development can proceed without waiting for hardware modules.

## Guide-level explanation

Developers should read the AP runtime as a pipeline:

```text
module or simulator event
  -> protocol validation
  -> battle runtime state machine
  -> domain rule update
  -> append-only event log
  -> live snapshot delivery
```

The simulator is a first-class development input. It should be able to create battles, join players,
emit shots, emit hits, and end battles through the same AP-facing event contract that future hardware
gateways will use.

## Reference-level explanation

### Module boundaries

- `apps/apd`: process entrypoint, CLI, config loading, logging, signal handling.
- `src/core`: domain types and rules such as battle id, player id, team id, health, score, and
  battle lifecycle.
- `src/protocol`: AP event DTOs, envelope validation, schema version checks, and serialization.
- `src/ap_runtime`: orchestration of battle sessions, event deduplication, state transitions, and
  fanout to storage/server.
- `src/storage`: append-only JSONL event log and battle record snapshots.
- `src/server`: HTTP POST command/event ingress and WebSocket live snapshot delivery.
- `src/simulator`: host-side generator for AP events before module integration.

### Initial AP lifecycle

```text
Cold
  -> load config
  -> start AP runtime
  -> accept battle setup
  -> battle active
  -> battle ended
  -> archive available
  -> shutdown
```

### First-version transport and persistence

The first AP version uses:

- HTTP POST for explicit commands and simulator event ingress.
- WebSocket for live battle snapshots and accepted event notifications.
- JSONL for append-only accepted event storage.

HTTP POST keeps command submission simple and debuggable. WebSocket is selected instead of SSE
because the operator UI is expected to evolve toward bidirectional control. JSONL is selected
instead of SQLite because the first milestone needs transparent event replay and easy inspection
more than indexed historical query.

### Development container ownership

The Podman development container must mirror the host user name, uid, and gid. Container writes to
the repository mount must therefore appear on the host as the developer's own files, not as `root`.
The mount layout follows the `embedev` model: code, tool/cache state, and user configuration are
mounted explicitly instead of mounting a whole synthetic home directory.

The container helper owns persistent host mounts under `~/.local/share/fenghuo` by default:

- `build` for container-side build artifacts.
- `cache` for generic tool caches.
- `ccache` for compiler cache.
- `deps` for future third-party dependency installs.
- `home-cache` for Fenghuo-specific home cache state.

`FENGHUO_DATA_ROOT` may override this location for machines that keep development state on a
separate disk.

### Invariants

- Event ingestion must pass through `src/protocol` before mutating battle state.
- `src/core` must not depend on HTTP, WebSocket, storage files, or hardware transports.
- `src/ap_runtime` owns battle session state transitions and deduplication.
- `src/storage` records accepted events in append-only JSONL order.
- Live API snapshots are derived from accepted AP state, not from raw unvalidated input.

### Failure modes

- Invalid events are rejected and recorded as diagnostics, but must not crash the AP process.
- Duplicate events must not apply game effects twice.
- Storage write failure makes the event not durably accepted; AP runtime must surface this as an
  operational error before broadcasting the event as accepted.
- UI WebSocket disconnects must not affect battle state.

### Protocol impact

The initial semantic protocol is documented in `src/protocol/rfd/0001-ap-event-protocol-v0.md`. It is an AP
ingestion contract, not a physical module wire protocol.

## Drawbacks

- The project gains documentation and module boundaries before visible functionality exists.
- The V0 protocol may need migration once real hardware timing and delivery constraints are known.
- AP development must maintain a simulator path in parallel with future module integration.

## Rationale and alternatives

Starting with AP runtime and simulator wins because it unblocks UI, scoring, storage, and live state
development without pretending that the hardware protocol is already known.

Starting from module firmware was rejected for this phase because the user-facing AP behavior can be
designed and tested with simulated events first.

Building one monolithic AP service was rejected because it would make protocol validation, game
rules, persistence, and live delivery hard to test independently.

## Prior art

- `embedev` module-local RFD workflow.
- `embedev` scheduler pattern: thin app entrypoint plus runtime orchestrator.
- Event-sourced game servers that keep accepted events as the durable audit trail.

## Unresolved questions

- Whether AP should own battle creation through REST commands or through the same event envelope.
- What latency budget should live battle updates target on the deployed AP hardware?

## Validation and rollout

- Unit: protocol envelope validation, duplicate detection, battle rule transitions, score updates.
- Module-local integration: simulator event sequence produces expected battle snapshot and event log.
- Cross-module integration: AP server accepts simulated events through HTTP POST and streams live
  snapshots through WebSocket.
- Migration or rollout: start with simulator-only AP ingestion; add module gateway after a separate
  protocol RFD. Bootstrap development through the Podman container when host C++ tools are missing.
- Deferred gaps: real infrared hit feedback, module acknowledgement, anti-cheat, clock sync, wireless
  reliability semantics, and current host validation until the container image is built.

## Decision log

- 2026-05-19: Draft created for AP-first Fenghuo runtime architecture and simulator protocol.
- 2026-05-19: Selected WebSocket for live UI updates, HTTP POST for command/event ingress, and
  JSONL for first-version accepted event storage.
- 2026-05-19: Added initial CMake/just/container bootstrap. Host validation could not run because
  `just` and `cmake` are not installed on the host; use the Podman container workflow.
- 2026-05-19: Validation passed in container with `base/container/podman/cli/fenghuo.sh run just
  test`; the minimal AP daemon builds and CTest reports no tests yet.
- 2026-05-19: Added container user/uid/gid mirroring and persistent host-mounted resource
  directories for build, cache, ccache, deps, and container home state.
- 2026-05-19: Validation passed after enabling Podman `--userns keep-id`: container `id` reports
  `zhangkunjie 1005 zhangkunjie 1005`, a container-created repository file has the same host owner
  and group, and `base/container/podman/cli/fenghuo.sh run just test` still builds successfully.

## Future possibilities

- Add `diag/` state and sequence facts once the AP runtime has stable behavior.
- Generate protocol DTOs from schema files.
- Add module feedback commands for hit confirmation, vibration, light, and audio cues.
- Add replay tooling that can rebuild battle snapshots from event logs.
