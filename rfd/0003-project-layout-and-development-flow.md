# RFD: Project layout and development flow

- Status: Draft
- Owners: fenghuo
- Created: 2026-05-19
- Updated: 2026-05-19

## Summary

Fenghuo uses module-local RFDs as the durable source of design context. The AP project is organized
around a thin process entrypoint, a runtime orchestrator, protocol validation, pure core rules,
storage, server transport, and simulator input.

## Motivation

The project needs a simple but explicit layout before AP code grows. Putting all design notes under
module-local `rfd/` directories keep explanations close to decision history and avoid scattered
README files that drift from the implementation plan.

## Guide-level explanation

Current layout:

```text
fenghuo/
├── AGENTS.md
├── CMakeLists.txt
├── justfile
├── rfd/                 # repository-level workflow and bootstrap decisions
├── apps/apd/
├── apps/rfd/            # AP application/runtime decisions
├── src/core/
├── src/protocol/
├── src/protocol/rfd/    # protocol decisions
├── src/ap_runtime/
├── src/server/
├── src/storage/
├── src/simulator/
└── tests/
```

Development flow:

1. Write or update the owning RFD for non-trivial work.
2. Define protocol or API contracts before implementation.
3. Implement the smallest module slice that satisfies the accepted design.
4. Add focused tests for invariants, failure modes, and externally visible behavior.
5. Record validation commands and remaining gaps in the RFD.

## Reference-level explanation

`apps/apd` is the AP process entrypoint. It owns CLI parsing, config loading, logging, signal
handling, starting `src/ap_runtime`, and converting fatal errors into process exit status. It must
not own battle rules, protocol parsing, or persistence internals.

`src/core` owns Fenghuo battle domain logic: battle, player, team, weapon, health, score, lifecycle
types, deterministic rule application, and snapshot construction. It must not depend on
HTTP/WebSocket, file/database persistence, hardware transport, or simulator shortcuts.

`src/protocol` owns AP-facing message contracts: event envelope DTOs, schema version checks, payload
validation, serialization/deserialization, and conversion from validated protocol messages into
runtime commands or core-domain events.

`src/ap_runtime` owns AP-side orchestration: battle session lifecycle, accepted event ordering and
deduplication, coordination between protocol/core/storage/server live snapshots, runtime error
handling, and shutdown.

`src/server` owns AP network APIs for operators, UI clients, and simulator ingress: HTTP command
endpoints, WebSocket live battle snapshot streaming, request/response validation at the transport
boundary, and forwarding validated messages into `src/ap_runtime`.

`src/storage` owns durable AP records: append-only accepted event log, battle metadata and final
record snapshots, and recovery helpers for rebuilding state from accepted events. Storage failures
must be visible to `src/ap_runtime` before the AP reports an event as accepted.

`src/simulator` owns host-side battle event generation before module hardware exists. It generates
player joins, battle start/end, shot, hit, and state events through the same protocol contract as
future module gateways.

## Drawbacks

- Centralizing explanatory markdown under `rfd/` means source directories have less local prose.
- The RFD list can grow quickly if each small idea becomes a separate document.

## Rationale and alternatives

Keeping explanatory README files in every source directory was rejected at this stage because the
user requested markdown explanations under the owning module's `rfd/`. Minimal code comments and
source structure should carry local meaning once implementation starts.

## Prior art

- `embedev` module-local RFD workflow.
- `embedev` thin app plus scheduler/runtime split.

## Unresolved questions

- Whether later mature modules should reintroduce minimal local README files for IDE navigation.

## Validation and rollout

- Unit: not applicable for documentation-only layout decision.
- Module-local integration: future code should follow these boundaries.
- Cross-module integration: `just test` after implementation slices.
- Migration or rollout: move existing explanatory markdown into owning module `rfd/` directories and
  remove source README files.
- Deferred gaps: no mechanical linter enforces these boundaries yet.

## Decision log

- 2026-05-19: Draft created and existing explanatory markdown consolidated under `rfd/`.

## Future possibilities

- Add generated architecture diagrams under `diag/`.
- Add a boundary linter for forbidden include dependencies.
