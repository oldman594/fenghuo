# Fenghuo Agent Rules

## Source Of Truth

- Repository files are the source of truth.
- Agents and developers must gather local evidence before making non-trivial changes.
- Chat history, memory, and undocumented assumptions are not authoritative.
- Module-local `rfd/` documents record design intent, accepted boundaries, validation plans, and known gaps.
- Protocol RFDs under the owning protocol module define wire/API contracts until generated schemas
  or IDL files exist.

## Workflow

- Non-trivial work must create or update a module-local `rfd/` document before implementation.
- Architecture, API, protocol, state-machine, persistence, recovery, or cross-module changes must use an RFD.
- A change that affects externally observable behavior must update the owning RFD or record why no document change is needed.
- Agents must design interfaces, ownership boundaries, invariants, failure modes, acceptance criteria, and validation strategy before implementation.
- Prefer small, reviewable changes. Each atomic change should represent one intent.
- Git commits are owned by the human maintainer. Agents must not run `git commit` unless the
  maintainer explicitly asks for a specific commit operation in the current turn.
- Agents must never run `git push`.
- Do not format unrelated files. Use the project formatter only when a matching project command exists.

## C++ Development

- C++ code should be C++20 unless an RFD accepts a different standard.
- Keep public headers small and ownership-explicit.
- Prefer value types for domain data, RAII for resources, and narrow interfaces for runtime adapters.
- Avoid global mutable state except for process-level singletons explicitly justified in an RFD.
- Use `std::chrono` types for time and duration at API boundaries.
- Use fixed-width integer types for protocol, storage, and externally visible IDs.
- Do not mix transport parsing, game rules, persistence, and HTTP/WebSocket delivery in one module.
- Tests should describe objective, construction method, input data, and expected behavior in comments when the behavior is non-obvious.

## Build And Validation

- `just` is the preferred entrypoint once a `justfile` exists.
- If a matching `just` target exists, use it instead of ad hoc commands.
- After relevant code changes, run the narrowest documented test target that covers the change; use `just test` before integration milestones.
- Record validation commands and remaining gaps in the owning RFD.

## RFD Placement

- Single-module work writes to `<module>/rfd/`.
- Work spanning multiple sibling modules writes to the nearest shared ancestor's `rfd/`.
- Repository workflow changes write to `fenghuo/rfd/`.
- Use four-digit numbering: `0001-short-title.md`.

## Current Project Scope

- The first implementation target is the AP-side battle runtime.
- Infrared gun/module firmware and hit feedback transport are out of scope until an RFD accepts the module-side protocol.
- AP development may use simulators to inject battle events before hardware integration exists.
