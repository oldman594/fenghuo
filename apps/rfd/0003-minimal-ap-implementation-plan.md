# RFD: Minimal AP implementation plan

- Status: Implemented
- Owners: apps
- Created: 2026-05-19
- Updated: 2026-05-19

## Summary

The minimal AP implementation is delivered in two milestones. Milestone 1 builds the AP kernel
without network transport: core battle rules, protocol-shaped event objects, JSONL storage, and
`ap_runtime` scheduling with fake broadcaster tests. Milestone 2 adds HTTP POST ingestion and
WebSocket live broadcast on top of that already-tested kernel.

As of 2026-05-19, Phases 2 through 6 have a minimal implementation:

- `src/protocol` parses and validates AP event JSON envelopes.
- `src/storage` appends accepted events to per-battle JSONL files and can read them back for replay.
- `src/ap_runtime` schedules accepted events with duplicate detection, sequence checks, transactional
  state commit, storage-before-broadcast, JSONL replay, and fake sink coverage.
- `src/server` exposes Beast HTTP `POST /api/v0/events`,
  `GET /api/v0/battles/{battle_id}/snapshot`, and WebSocket `/api/v0/live`.
- `apps/apd` wires the JSONL store, runtime, WebSocket broadcaster, and server.

## Motivation

The project has an accepted AP V0 event-loop design, but implementation should not begin with HTTP
handlers. If the first code slice starts at `src/server`, JSON parsing, deduplication, scoring,
persistence, and broadcasting can collapse into one transport handler. The minimum AP should prove
the internal scheduler loop first, then expose it through network endpoints.

This RFD turns the V0 design into an implementation plan with concrete interfaces, module order, and
acceptance tests.

## Guide-level explanation

Developers should implement the AP in this order:

```text
Phase 1: core domain model and rules
Phase 2: protocol event DTOs and validation model
Phase 3: JSONL storage interface and append implementation
Phase 4: ap_runtime scheduler with fake broadcaster
Phase 5: HTTP POST + WebSocket server
Phase 6: app wiring in fenghuo-apd
```

Milestone 1 is complete when tests can submit a sequence directly to `ApRuntime::submit_event()`:

```text
player_joined
player_joined
battle_started
shot
hit
battle_ended
```

and verify:

- accepted events were appended to JSONL
- the final snapshot has expected player health, shot counts, hit counts, team score, and lifecycle
- duplicate events do not apply effects twice
- storage failure prevents state commit and broadcast

Milestone 2 is complete when the same sequence works through HTTP POST and live WebSocket clients
receive snapshots.

## Reference-level explanation

### Phase 1: `src/core`

Purpose: deterministic battle rules without network, files, clocks, or simulator shortcuts.

Initial public types:

```cpp
namespace fenghuo::core {

struct BattleId;
struct PlayerId;
struct TeamId;
struct EventId;

enum class BattlePhase {
    Lobby,
    Active,
    Ended,
};

struct PlayerState;
struct TeamState;
struct BattleState;
struct BattleSnapshot;

struct PlayerJoined;
struct BattleStarted;
struct Shot;
struct Hit;
struct PlayerStateUpdated;
struct BattleEnded;
using BattleEvent = std::variant<
    PlayerJoined,
    BattleStarted,
    Shot,
    Hit,
    PlayerStateUpdated,
    BattleEnded>;

} // namespace fenghuo::core
```

Core rule entrypoint:

```cpp
Result<BattleSnapshot> apply_event(BattleState& state, const BattleEvent& event);
```

Rules:

- A new `BattleState` starts in `Lobby`.
- `PlayerJoined` is accepted only in `Lobby`.
- `BattleStarted` moves `Lobby -> Active`.
- `Shot`, `Hit`, and `PlayerStateUpdated` are accepted only in `Active`.
- `Hit` requires known attacker and target players.
- `Hit` subtracts `damage` from target health, clamps health at `0`, increments attacker hit count,
  and increments attacker team score by one for V0.
- `BattleEnded` moves `Active -> Ended`.
- Events other than exact duplicate handling are rejected after `Ended`; duplicate handling belongs
  to `ap_runtime`, not core.

### Phase 2: `src/protocol`

Purpose: represent and validate AP event envelopes before runtime/core use.

Initial types:

```cpp
namespace fenghuo::protocol {

struct EventEnvelope {
    int schema_version;
    std::string event_id;
    std::string event_type;
    std::string battle_id;
    std::string source_id;
    std::uint64_t sequence;
    std::int64_t occurred_at_ms;
    Payload payload;
};

Result<EventEnvelope> parse_event_json(std::string_view json);
Result<core::BattleEvent> to_core_event(const EventEnvelope& envelope);

} // namespace fenghuo::protocol
```

V0 may initially build `EventEnvelope` values directly in tests before JSON parsing is introduced,
but the interfaces must leave room for JSON parsing.

Implemented files:

- `src/protocol/event.hpp`
- `src/protocol/event.cpp`

Validation split:

- `src/protocol` rejects malformed JSON, unsupported schema, unknown event type, missing fields, and
  payload shape mismatches.
- `src/ap_runtime` rejects duplicates, stale sequence conflicts, unknown battles, and lifecycle
  conflicts.
- `src/core` rejects rule violations that depend only on current battle state and event content.

### Phase 3: `src/storage`

Purpose: durable accepted event log.

Interface:

```cpp
namespace fenghuo::storage {

class BattleEventStore {
public:
    virtual ~BattleEventStore() = default;
    virtual Result<void> append(const protocol::EventEnvelope& envelope,
                                const AcceptedEventMetadata& metadata) = 0;
};

class JsonlBattleEventStore final : public BattleEventStore {
public:
    explicit JsonlBattleEventStore(std::filesystem::path root);
    Result<void> append(const protocol::EventEnvelope& envelope,
                        const AcceptedEventMetadata& metadata) override;
};

} // namespace fenghuo::storage
```

V0 file layout:

```text
<event_log_root>/<battle_id>.jsonl
```

Each line is one accepted event plus AP metadata. Rejected events are not written to this accepted
event log.

Implemented files:

- `src/storage/event_store.hpp`
- `src/storage/jsonl_event_store.cpp`

### Phase 4: `src/ap_runtime`

Purpose: AP scheduler. This is the first actual AP loop.

Interfaces:

```cpp
namespace fenghuo::ap_runtime {

struct SubmitEventResult {
    enum class Status {
        Accepted,
        Duplicate,
        Rejected,
        StorageFailed,
    };
    Status status;
    core::BattleSnapshot snapshot;
};

class BattleUpdateSink {
public:
    virtual ~BattleUpdateSink() = default;
    virtual void publish_accepted_event(const protocol::EventEnvelope& envelope,
                                        const core::BattleSnapshot& snapshot) = 0;
};

class ApRuntime {
public:
    SubmitEventResult submit_event(const protocol::EventEnvelope& envelope);
};

} // namespace fenghuo::ap_runtime
```

Acceptance order:

1. Check `event_id` duplicate table.
2. Check sender sequence diagnostics.
3. Convert envelope to core event.
4. Apply core event to a copy or transactional state.
5. Append accepted event to store.
6. Commit state and duplicate table.
7. Publish accepted event and snapshot.
8. Return accepted result.

Storage-before-broadcast is mandatory.

Milestone 1 uses fake implementations:

- `FakeBattleEventStore`
- `FailingBattleEventStore`
- `CollectingBattleUpdateSink`

Implemented files:

- `src/ap_runtime/runtime.hpp`
- `src/ap_runtime/runtime.cpp`
- `tests/ap_loop_test.cpp`

### Phase 5: `src/server`

Purpose: transport adapter over `ApRuntime`.

Endpoints:

```text
POST /api/v0/events
GET  /api/v0/battles/{battle_id}/snapshot
WS   /api/v0/live
```

Server must not implement game rules, deduplication, or storage policy.

HTTP status mapping follows `apps/rfd/0002-ap-v0-event-loop.md`.

Implemented files:

- `src/server/server.hpp`
- `src/server/server.cpp`

V0 server implementation is intentionally simple: one accept loop creates one thread per HTTP or
WebSocket connection. `ApServer::stop()` closes the acceptor and active sockets, stops WebSocket
sessions, joins server-owned worker threads, and lets `run()` return success. A later RFD should
revisit fully async session ownership, backpressure, and integration tests with a real WebSocket
client.

### Phase 6: `apps/apd`

Purpose: process entrypoint.

Responsibilities:

- parse config path and log level
- load AP config
- initialize storage root
- construct `ApRuntime`
- start server
- handle SIGINT/SIGTERM
- exit nonzero on fatal startup/runtime failure

Implemented behavior:

- Parses `--port PORT`.
- Parses `--event-log-root PATH`.
- Creates `JsonlBattleEventStore`.
- Creates `ApRuntime`.
- Replays accepted JSONL records into `ApRuntime` before accepting new traffic.
- Creates `WebSocketBroadcaster`.
- Starts Beast HTTP/WebSocket server.
- Handles SIGINT/SIGTERM by calling `ApServer::stop()`.

Implemented file:

- `apps/apd/main.cpp`

## Drawbacks

- Splitting milestones delays visible HTTP/WebSocket functionality.
- The first milestone needs fake store and fake broadcaster code that is not production transport.
- Some interfaces may change after JSON parsing and concrete server dependencies are selected.

## Rationale and alternatives

Starting with `src/core` and `src/ap_runtime` wins because the AP behavior becomes testable without
network timing or library choices. Starting with `src/server` was rejected because it would make
transport code the accidental owner of domain rules.

Implementing WebSocket before JSONL was rejected because the AP should not broadcast accepted events
that have not been durably recorded.

## Prior art

- `embedev` scheduler pattern: process entrypoint delegates runtime orchestration to a scheduler-like
  module.
- `apps/rfd/0002-ap-v0-event-loop.md`, which defines the full V0 AP loop.
- Event-sourced services that test command handling before transport adapters.

## Unresolved questions

- Whether battle creation is implicit on first `PlayerJoined` for a `battle_id` or explicit through a
  separate command.
- Whether `BattleStarted` should be rejected with fewer than two players.

## Validation and rollout

- Unit:
  - `src/core`: lifecycle transitions, hit scoring, health clamp, unknown player rejection.
  - `src/protocol`: valid envelopes, malformed JSON, unsupported schema, missing fields.
  - `src/storage`: JSONL append writes one line per accepted event and surfaces write failures.
  - `src/ap_runtime`: duplicate behavior, storage-before-broadcast, lifecycle rejection, happy-path
    sequence.
- Module-local integration:
  - direct `ApRuntime::submit_event()` sequence with fake store and fake broadcaster.
  - JSONL store writes accepted events for one battle and leaves rejected events out.
- Cross-module integration:
  - `fenghuo-apd` starts once server is introduced.
  - HTTP POST and WebSocket smoke after Phase 5.
  - `base/container/podman/cli/fenghuo.sh run just test`.
- Completed validation:
  - `base/container/podman/cli/fenghuo.sh run just test`
  - `fenghuo.ap_loop` covers JSONL replay by writing the normal event sequence, creating a fresh
    runtime, replaying accepted records, verifying restored snapshot state, and verifying duplicate
    state is restored without appending another event.
  - `fenghuo.server_integration` starts an in-process AP server, connects a real Beast WebSocket
    client, POSTs the normal event sequence over HTTP, verifies live accepted-event messages, queries
    the HTTP snapshot endpoint, and verifies JSONL accepted-event count.
  - `base/container/podman/cli/fenghuo.sh run ./build/fenghuo-apd --help`
  - `base/container/podman/cli/fenghuo.sh run ./build/fenghuo-apd --port 0`
  - `base/container/podman/cli/fenghuo.sh run bash -lc './build/fenghuo-apd --port 18082 --event-log-root /tmp/fenghuo-events-sigterm & pid=$!; sleep 0.2; kill -TERM "$pid"; wait "$pid"'`
- Migration or rollout:
  - implement phases in order unless an RFD updates the sequence.
  - keep server disabled or stubbed until runtime tests pass.
  - keep module hardware feedback out of scope until a module transport RFD exists.
- Deferred gaps:
  - bounded WebSocket write queues and backpressure policy.
  - physical infrared module feedback.
  - authentication and operator authorization.
  - anti-cheat.
  - time synchronization.
  - SQLite projections.

## Decision log

- 2026-05-19: Draft created for minimal AP implementation plan.
- 2026-05-19: Resolved implementation technology choices through
  `rfd/0006-ap-v0-technology-selection.md`: project-local `Result<T>`, `nlohmann/json`, and
  Boost.Beast/Asio.
- 2026-05-19: Implemented minimal Phase 2 through Phase 6 chain with protocol parsing, JSONL
  storage, AP runtime scheduler, Beast HTTP/WebSocket adapter, and `apd` process wiring. Container
  validation passed with two CTest tests: `fenghuo.result` and `fenghuo.ap_loop`.
- 2026-05-19: Added stoppable server lifecycle and SIGINT/SIGTERM handling. `ApServer::stop()`
  closes acceptor/active sockets, stops WebSocket sessions, joins worker threads, and returns
  success from `run()`. Container SIGTERM smoke test exits with code 0.
- 2026-05-19: Added real HTTP/WebSocket integration coverage in `fenghuo.server_integration`.
  Container validation now passes three CTest tests: `fenghuo.result`, `fenghuo.ap_loop`, and
  `fenghuo.server_integration`.
- 2026-05-19: Implemented JSONL replay. `JsonlBattleEventStore::read_all()` reads accepted records
  from per-battle logs, `ApRuntime::replay()` rebuilds battle snapshots, duplicate event state, source
  sequence state, and acceptance sequence, and `apps/apd` replays records before starting the server.

## Future possibilities

- Add `diag/` state and sequence diagrams once Phase 4 stabilizes.
- Add generated protocol schemas after Phase 2.
- Add benchmark or soak tests for WebSocket fanout after Phase 5.
