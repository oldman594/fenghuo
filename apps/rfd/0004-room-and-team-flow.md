# RFD: AP room and team flow

- Status: Draft
- Owners: apps
- Created: 2026-05-21
- Updated: 2026-05-21

## Summary

Fenghuo should add a pre-battle room flow before expanding the physical module protocol. A room is
the operator-visible lobby where players enter, choose teams, become ready, and start a battle. The
AP should expose simple HTTP command endpoints for UI and command-line clients, persist accepted
room changes as append-only events, and publish room and battle updates over the existing WebSocket
channel.

## Motivation

The current minimal AP can process a complete simulated battle sequence, but it has no user-facing
setup flow. Operators still need a way to create a room, let players join, assign or change teams,
mark readiness, and start a battle without hand-writing low-level `player_joined` events.

The next implementation should keep the current AP event runtime intact while adding a room layer in
front of it. This prevents UI concerns from leaking into battle rules and gives developers a stable
manual testing path before building a richer web UI or mobile app.

## Guide-level explanation

The AP should expose two related workflows:

```text
Room setup workflow:
  create room
  -> join players
  -> assign teams
  -> mark players ready
  -> start room

Battle workflow:
  room start creates or opens a battle
  -> AP submits player_joined events for the room roster
  -> AP submits battle_started
  -> modules or simulators submit shot/hit/player_state events
  -> AP submits battle_ended
```

Room commands are for operators, UI clients, test scripts, and local development tools. Battle events
remain the AP ingestion contract for simulator or future module-generated gameplay events.

The recommended first testing surface is command-line scripts because they are fast, repeatable, and
easy to run in CI. A small web UI should come after the HTTP command contract stabilizes. A mobile
app should wait until the room and live update semantics are proven.

## Reference-level explanation

### Room state model

Initial room fields:

```text
room_id
room_code
name
mode
phase
created_at_ms
updated_at_ms
host_player_id
max_players
teams
players
battle_id
latest_room_event_id
```

Initial room phases:

```text
open
active
ended
closed
```

`open` means players may join, leave, change teams, and change ready state.

`active` means the battle has started. The roster is frozen for V1.

`ended` means the linked battle has ended and the room snapshot remains queryable.

`closed` means the room is no longer accepting commands. Closed rooms may still be retained for
debugging or later history views.

Initial player fields:

```text
player_id
display_name
team_id
module_id
ready
joined_at_ms
```

Initial team fields:

```text
team_id
display_name
max_players
```

### Proposed HTTP endpoints

Room command endpoints should be explicit resources instead of overloading the existing
`POST /api/v0/events` gameplay endpoint:

```text
POST /api/v0/rooms
GET  /api/v0/rooms
GET  /api/v0/rooms/{room_id}
POST /api/v0/rooms/{room_id}/players
DELETE /api/v0/rooms/{room_id}/players/{player_id}
POST /api/v0/rooms/{room_id}/players/{player_id}/team
POST /api/v0/rooms/{room_id}/players/{player_id}/ready
POST /api/v0/rooms/{room_id}/start
POST /api/v0/rooms/{room_id}/close
```

Example create room request:

```json
{
  "name": "Friday match",
  "mode": "team_deathmatch",
  "max_players": 8,
  "teams": [
    {"team_id": "red", "display_name": "Red", "max_players": 4},
    {"team_id": "blue", "display_name": "Blue", "max_players": 4}
  ]
}
```

Example join room request:

```json
{
  "player_id": "p-red-01",
  "display_name": "Red 01",
  "team_id": "red",
  "module_id": "module-red-01"
}
```

Example change team request:

```json
{
  "team_id": "blue"
}
```

Example ready request:

```json
{
  "ready": true
}
```

Example start request:

```json
{
  "duration_ms": 600000
}
```

Room responses should return the current room snapshot:

```json
{
  "ok": true,
  "room": {
    "room_id": "room-001",
    "room_code": "483921",
    "phase": "open",
    "mode": "team_deathmatch",
    "players": {},
    "teams": {}
  }
}
```

### Accepted room events

Although callers use resource-style HTTP endpoints, the AP should persist accepted room changes as
room events. This keeps replay, debugging, and WebSocket fanout aligned with the existing AP event
style without forcing UI clients to construct low-level envelopes.

Initial room event types:

```text
room_created
room_player_joined
room_player_left
room_player_team_changed
room_player_ready_changed
room_started
room_closed
```

Room events should be written to:

```text
<event-log-root>/rooms/<room_id>.jsonl
```

The linked battle should continue to use:

```text
<event-log-root>/<battle_id>.jsonl
```

### Start room behavior

`POST /api/v0/rooms/{room_id}/start` should perform one transaction-like operation:

1. Validate that the room is `open`.
2. Validate that the room has enough players for the selected mode.
3. Validate that every joined player is ready unless the request explicitly allows force start.
4. Validate team capacity and minimum team requirements.
5. Persist `room_started`.
6. Create a `battle_id` linked to the room.
7. Submit one `player_joined` battle event for each room player.
8. Submit `battle_started`.
9. Commit the room phase as `active`.
10. Publish room and battle updates over WebSocket.

For V1, if any battle event cannot be accepted after `room_started` is persisted, the API should
return an error and mark the room with a startup failure diagnostic. A later version can introduce a
proper multi-log transaction or a single combined event store.

### WebSocket updates

The existing WebSocket endpoint can remain:

```text
WS /api/v0/live
```

It should publish both room and battle messages:

```json
{
  "type": "room_updated",
  "room": {}
}
```

```json
{
  "type": "accepted_event",
  "event": {},
  "snapshot": {}
}
```

This lets a minimal web UI subscribe once and update lobby state and battle state from the same
connection.

### Module responsibilities

`src/room` should own pure room domain rules:

- room lifecycle
- player join and leave
- team assignment
- ready state
- room snapshot construction

`src/room_runtime` or an `ap_runtime` submodule should own orchestration:

- room registry
- room event deduplication
- room event storage
- room-to-battle start coordination
- WebSocket room fanout

`src/server` should own transport:

- HTTP request parsing for room commands
- response status mapping
- no room business rules

`src/storage` should gain room JSONL support:

- append accepted room events
- replay room events on startup

`tools/demo` should gain command-line room sequences:

- normal room start
- duplicate join
- full team rejection
- ready required before start

### Invariants

- A player id is unique within one room.
- A module id is unique within one room when present.
- Team changes are accepted only while the room is `open`.
- Ready changes are accepted only while the room is `open`.
- `start` freezes the roster for V1.
- A room can link to at most one active battle.
- Battle gameplay events still go through the existing battle event protocol.
- Accepted room changes are persisted before WebSocket publication.

### Failure semantics

Protocol or request failures:

- malformed JSON -> HTTP 400
- missing required field -> HTTP 400
- invalid room id or player id shape -> HTTP 400

Room rule failures:

- unknown room -> HTTP 404
- duplicate player -> HTTP 409
- unknown player -> HTTP 404
- unknown team -> HTTP 400 or HTTP 404
- team full -> HTTP 409
- start with no players -> HTTP 409
- start with unready players -> HTTP 409
- command not allowed in current room phase -> HTTP 409

Storage failures:

- room event append failure -> HTTP 500
- no room state mutation is committed
- no WebSocket update is published

### Testing strategy

Testing should be layered.

Unit tests:

- create room default state
- join player
- reject duplicate player
- reject full team
- change team
- reject team change after start
- change ready state
- reject start when players are not ready
- start room and freeze roster

Integration tests:

- HTTP create room returns a room snapshot
- HTTP join/change team/ready updates the snapshot
- HTTP start emits room and battle updates
- replay rebuilds room state after daemon restart
- WebSocket receives `room_updated`

Command-line test scripts:

```text
tools/demo/room_v1_sequence.sh
tools/demo/room_rejection_sequence.sh
```

The normal script should:

1. Create a room.
2. Join two players.
3. Put players on different teams.
4. Mark both players ready.
5. Start the room.
6. Query the room snapshot.
7. Query the battle snapshot.

The rejection script should:

1. Try a duplicate join.
2. Try to overfill a team.
3. Try to start with an unready player.
4. Try to change team after start.

Manual UI testing:

- Build a small local web UI after the command endpoints and WebSocket messages are stable.
- The first web UI should be a development console, not a polished product.
- It should support create room, join, team change, ready toggle, start, room snapshot, and live
  battle snapshot.

Mobile app testing:

- Defer until room semantics and module transport are stable.
- A mobile app adds packaging and network discovery complexity that does not help validate the first
  AP room rules.

### Suggested implementation order

1. Write room domain types and pure rule tests.
2. Add room event DTOs and JSON serialization.
3. Add room JSONL store and replay tests.
4. Add room runtime with accepted event ordering and duplicate handling.
5. Add HTTP room endpoints.
6. Add WebSocket `room_updated` messages.
7. Add `tools/demo/room_v1_sequence.sh`.
8. Add the minimal web UI development console.
9. Revisit mobile app or operator app requirements.

### Step 1: room domain model and pure rule tests

Step 1 introduces `src/room` with no dependency on HTTP, WebSocket, JSONL files, or battle runtime
orchestration. The goal is to make room behavior deterministic and cheap to test before it is exposed
through protocol, storage, or server code.

Initial public types:

```cpp
namespace fenghuo::room {

enum class RoomPhase {
    Open,
    Active,
    Ended,
    Closed,
};

struct RoomTeamState;
struct RoomPlayerState;
struct RoomState;
using RoomSnapshot = RoomState;

struct RoomCreated;
struct RoomPlayerJoined;
struct RoomPlayerLeft;
struct RoomPlayerTeamChanged;
struct RoomPlayerReadyChanged;
struct RoomStarted;
struct RoomEnded;
struct RoomClosed;
using RoomEvent = std::variant<...>;

Result<RoomSnapshot> apply_event(RoomState& state, const RoomEvent& event);
std::string to_string(RoomPhase phase);

} // namespace fenghuo::room
```

Rules accepted in Step 1:

- `room_created` initializes an empty room only when the current state has no `room_id`.
- `room_created` must define at least one team and a positive `max_players`.
- A room starts in `open`.
- `room_player_joined`, `room_player_left`, `room_player_team_changed`, and
  `room_player_ready_changed` are accepted only while the room is `open`.
- A player id is unique in the room.
- A non-empty module id is unique in the room.
- A player must join an existing team.
- A team cannot exceed `max_players`.
- The room cannot exceed `max_players`.
- A team change must target an existing team and cannot overfill that team.
- `room_started` is accepted only while the room is `open`.
- `room_started` requires at least one player.
- `room_started` requires all players to be ready.
- `room_started` freezes the roster and moves the room to `active`.
- `room_ended` is accepted only while the room is `active`.
- `room_closed` is accepted from `open` or `ended`, but not from `active`.
- Every accepted event updates `latest_room_event_id`.

Step 1 validation:

- `fenghuo-room-test` covers create, join, duplicate player, duplicate module, full team, team
  change, ready, start, lifecycle conflicts, end, and close.
- No server, storage, or protocol test should be required for Step 1.

### Step 2: room event DTO and JSON serialization

Step 2 adds an external JSON shape for room events and snapshots. It should mirror the existing AP
event parser style but remain separate from gameplay `protocol::EventEnvelope`.

Every accepted room event uses a common envelope:

```json
{
  "schema_version": 0,
  "event_id": "room-evt-001",
  "event_type": "room_player_joined",
  "room_id": "room-001",
  "source_id": "operator-ui",
  "sequence": 2,
  "occurred_at_ms": 1730000000002,
  "payload": {}
}
```

Step 2 public API:

```cpp
namespace fenghuo::room {

struct RoomEventEnvelope;

Result<RoomEventEnvelope> parse_room_event_json(std::string_view json);
Result<RoomEvent> to_room_event(const RoomEventEnvelope& envelope);
nlohmann::json to_json(const RoomEventEnvelope& envelope);
nlohmann::json to_json(const RoomSnapshot& snapshot);

} // namespace fenghuo::room
```

The room envelope deliberately lives in `src/room/protocol.*` instead of `src/protocol` for now.
Gameplay AP events and room commands have different lifecycles and should not share one parser until
there is a concrete reason to merge them.

Acceptance criteria:

- parse valid room event JSON into domain events
- reject malformed JSON
- reject missing required fields
- reject unknown room event types
- serialize room snapshots for HTTP and WebSocket responses

### Step 3: room JSONL storage and replay

Step 3 adds append-only room event storage under:

```text
<event-log-root>/rooms/<room_id>.jsonl
```

Room JSONL records should mirror battle event records:

```json
{
  "event": {},
  "metadata": {
    "received_at_ms": 1730000000100,
    "accepted_at_ms": 1730000000110,
    "acceptance_sequence": 1
  }
}
```

Step 3 public API:

```cpp
namespace fenghuo::storage {

struct AcceptedRoomEventRecord;

class RoomEventStore {
public:
    virtual Result<void> append(const room::RoomEventEnvelope& envelope,
                                const AcceptedEventMetadata& metadata) = 0;
};

class JsonlRoomEventStore final : public RoomEventStore {
public:
    Result<void> append(...);
    Result<std::vector<AcceptedRoomEventRecord>> read_all() const;
};

} // namespace fenghuo::storage
```

Acceptance criteria:

- accepted room events are appended with metadata
- replay reads all room event logs in deterministic order
- replay rebuilds room snapshots and duplicate event state

### Step 4: room runtime

Step 4 adds room orchestration around the pure domain rules.

Step 4 public API:

```cpp
namespace fenghuo::room_runtime {

struct SubmitRoomEventResult;

class RoomUpdateSink {
public:
    virtual void publish_room_updated(const room::RoomEventEnvelope& envelope,
                                      const room::RoomSnapshot& snapshot) = 0;
};

class RoomRuntime {
public:
    Result<void> replay(const std::vector<storage::AcceptedRoomEventRecord>& records);
    SubmitRoomEventResult submit_event(const room::RoomEventEnvelope& envelope);
    Result<room::RoomSnapshot> snapshot(std::string room_id) const;
    std::vector<room::RoomSnapshot> snapshots() const;
};

} // namespace fenghuo::room_runtime
```

Step 4 intentionally does not start battles yet. It records `room_started` and preserves the
`battle_id` in room state. Step 5 can expose room HTTP commands on top of this runtime, and a later
slice can decide exactly where room-to-battle event submission should live.

Acceptance criteria:

- runtime accepts room event DTOs transactionally
- duplicate room event ids do not apply effects twice
- storage failure blocks state commit and WebSocket publication
- room snapshots can be queried by `room_id`
- start-room orchestration can produce linked battle setup events

### Step 5: HTTP room endpoints

Step 5 exposes room commands through `src/server`:

```text
POST /api/v0/rooms
GET  /api/v0/rooms
GET  /api/v0/rooms/{room_id}
POST /api/v0/rooms/{room_id}/players
DELETE /api/v0/rooms/{room_id}/players/{player_id}
POST /api/v0/rooms/{room_id}/players/{player_id}/team
POST /api/v0/rooms/{room_id}/players/{player_id}/ready
POST /api/v0/rooms/{room_id}/start
POST /api/v0/rooms/{room_id}/close
```

Endpoint-to-event mapping:

```text
POST /api/v0/rooms
  -> room_created
POST /api/v0/rooms/{room_id}/players
  -> room_player_joined
DELETE /api/v0/rooms/{room_id}/players/{player_id}
  -> room_player_left
POST /api/v0/rooms/{room_id}/players/{player_id}/team
  -> room_player_team_changed
POST /api/v0/rooms/{room_id}/players/{player_id}/ready
  -> room_player_ready_changed
POST /api/v0/rooms/{room_id}/start
  -> room_started
POST /api/v0/rooms/{room_id}/close
  -> room_closed
```

V1 may accept optional caller-provided `room_id`, `room_code`, `event_id`, `source_id`, `sequence`,
and `occurred_at_ms` fields for deterministic tests. If omitted, the AP generates practical defaults.

Acceptance criteria:

- successful commands return the current room snapshot
- invalid JSON returns HTTP 400
- unknown room/player returns HTTP 404
- rule conflicts return HTTP 409

### Step 6: WebSocket room updates

Step 6 extends `WS /api/v0/live` to publish:

```json
{
  "type": "room_updated",
  "room": {}
}
```

Acceptance criteria:

- room updates are published after accepted room commands
- room updates are not published for rejected commands
- existing battle `accepted_event` messages keep their current shape

### Step 7: command-line room demo scripts

Step 7 adds `tools/demo/room_v1_sequence.sh`.

Acceptance criteria:

- create room
- join two players
- set teams
- mark both players ready
- start room
- print room snapshot
- print linked battle id
- attempt to print linked battle snapshot when room-to-battle orchestration is available

### Step 8: minimal web development console

Step 8 adds a small AP-hosted web console for local manual testing. It should be served by
`fenghuo-apd` without npm, a separate web server, or a production UI framework.

Static files:

```text
apps/console/index.html
apps/console/styles.css
apps/console/app.js
```

Server routes:

```text
GET /console
GET /console/
GET /console/styles.css
GET /console/app.js
```

Console capabilities:

- list rooms from `GET /api/v0/rooms`
- create a room through `POST /api/v0/rooms`
- select a room and show its roster, teams, phase, and linked battle id
- close a room through `POST /api/v0/rooms/{room_id}/close`; V1 treats this as a soft delete
- join red and blue players through `POST /api/v0/rooms/{room_id}/players`
- toggle ready through `POST /api/v0/rooms/{room_id}/players/{player_id}/ready`
- start a room through `POST /api/v0/rooms/{room_id}/start`
- receive live `room_updated` messages from `WS /api/v0/live`
- show recent command responses and live events for debugging

Step 8 is a development console, not the final player/operator UI. It should stay small and
resource-oriented so it remains a manual testing tool for the room API.

### Step 9: room-to-battle start and simulator attacks

Step 9 connects room start to the existing battle runtime and adds a simple simulation surface.

Room start behavior:

- After `room_started` is accepted, AP submits one gameplay `player_joined` event for every room
  player.
- AP then submits `battle_started` with the room mode and requested duration.
- If battle setup fails, the room start response returns an error and the failure is visible in
  command logs.
- V1 keeps this orchestration in `src/server` because it bridges room HTTP commands and battle
  runtime. A later RFD can move it into a dedicated application service.

Manual attack behavior:

- The console can choose an attacker and target from the active room's linked battle.
- It submits `shot` and then `hit`.
- V1 only offers enemy targets in the UI and does not implement friendly-fire rule checks in core.
- `hit` reduces target health and increments attacker hit count and team score through existing
  battle rules.

End behavior:

- V1 supports manual `battle_ended` from the console.
- V1 supports `battle_paused` and `battle_resumed` for a started battle.
- While paused, `shot`, `hit`, and `player_state` are rejected; `battle_resumed` and
  `battle_ended` remain valid.
- The first automatic time-limit behavior may live in the browser simulator: when its local countdown
  expires, it submits `battle_ended` with `reason = "time_limit"`.
- Server-owned timers are deferred because they require lifecycle ownership, recovery rules after
  replay, and clock policy.

Browser clients must generate a unique `source_id` per tab/session and use monotonic sequences that
do not reset to small values after refresh. Otherwise the AP runtime correctly rejects commands as
stale source sequences.

Automatic simulator page:

```text
GET /sim
GET /sim/styles.css
GET /sim/app.js
```

The simulator uses a simple top-down map inspired by desert arena layouts: rectangular walls, two
spawn areas, and corridors. It is not a real CS map clone. Players move with a deterministic
steering loop, acquire enemy targets with line-of-sight, and post `shot`/`hit` events when in range.
Friendly targets are ignored in V1.

## Drawbacks

- The AP gains a second state machine before real module hardware is integrated.
- Room start crosses room state and battle state, which introduces transaction-like failure modes.
- Resource-style HTTP commands plus internal room events add an extra translation layer.
- A development web UI can drift from command-line tests if both are not kept small and focused.

## Rationale and alternatives

Using only `POST /api/v0/events` for room commands was rejected for V1 because UI clients should not
need to construct internal event envelopes for ordinary lobby actions.

Using only REST resources without accepted room events was rejected because replay and debugging
would diverge from the existing AP event-sourced design.

Building a mobile app first was rejected because command-line scripts and a small web UI validate the
server contract faster and with less platform overhead.

Allowing late joins after battle start is deferred. It is useful for some game modes, but it changes
scoring, module assignment, and fairness rules. V1 should freeze the roster at start.

## Prior art

- Existing AP V0 event loop and JSONL accepted event log.
- Multiplayer game lobby flows with ready checks and team assignment.
- Event-sourced room and match services that separate lobby commands from gameplay events.

## Unresolved questions

- Should `room_id` be client-provided, AP-generated, or both?
- Should `room_code` be numeric, human-readable words, or absent in local-only deployments?
- Should host permissions exist in V1, or can all clients issue room commands?
- Should start require all players ready, or should the host be allowed to force start?
- Should room event logs and battle event logs eventually share one combined transaction log?
- What is the minimum player/team requirement for each game mode?

## Validation and rollout

- Unit: room domain rule tests for join, leave, team, ready, start, and lifecycle conflicts.
- Module-local integration: room runtime replay and room-to-battle start tests.
- Cross-module integration: HTTP room endpoints plus WebSocket room updates and battle snapshots.
- Migration or rollout: keep the existing battle event endpoint unchanged, add room endpoints in
  parallel, then update demo docs after the command-line scripts pass.
- Deferred gaps: authentication, authorization, host transfer, late joins, room discovery across
  networks, production persistence, mobile app, and real module binding.

## Decision log

- 2026-05-21: Draft created for review before implementation order is accepted.

## Future possibilities

- Add QR-code room joining for mobile clients.
- Add operator permissions and host transfer.
- Add auto-balance and random team assignment.
- Add spectator clients.
- Add room history and match result archives.
- Add network discovery for AP devices on a local LAN.
