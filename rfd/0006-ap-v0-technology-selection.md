# RFD: AP V0 technology selection

- Status: Accepted
- Owners: fenghuo
- Created: 2026-05-19
- Updated: 2026-05-19

## Summary

AP V0 uses `nlohmann/json` for JSON parsing and serialization, Boost.Beast with Boost.Asio for HTTP
and WebSocket transport, and a project-local C++20 `Result<T>` / `Error` abstraction for recoverable
module errors. These choices are accepted for the minimal AP implementation loop.

## Motivation

The minimal AP plan needs concrete library choices before implementation begins. The choices must
support a C++20 codebase, run inside the Podman development container, avoid a heavy application
framework, and keep AP runtime logic independent from transport details.

Three decisions are required now:

- JSON library for AP event envelopes and JSONL records.
- HTTP/WebSocket library for `src/server`.
- Result/error abstraction for protocol, core, runtime, storage, and server boundaries.

## Guide-level explanation

Developers should use these defaults:

```text
JSON:          nlohmann/json
HTTP/WS:       Boost.Beast + Boost.Asio
Result/Error:  fenghuo::Result<T> and fenghuo::Error
```

`nlohmann/json` is used at the protocol and storage boundary only. Core battle rules should not
depend on JSON types.

Boost.Beast is used only under `src/server`. AP runtime and core modules should not include Beast or
Asio headers.

`Result<T>` is the normal return type for recoverable errors across project modules. Fatal process
startup errors may still be converted to nonzero process exit by `apps/apd`.

This RFD does not mean every module can include every selected dependency. The dependency boundary is
part of the decision:

- `src/protocol`: may use `nlohmann/json`; must not use Beast route/session types.
- `src/server`: may use Beast/Asio and `nlohmann/json`; must not own game rules.
- `src/storage`: may use `nlohmann/json` for JSONL records; must not own scoring rules.
- `src/ap_runtime`: may use `Result<T>` and plain project types; must not expose Beast/Asio.
- `src/core`: may use `Result<T>` and standard C++ types; must not depend on JSON, Beast, files, or
  sockets.

## Reference-level explanation

### JSON: `nlohmann/json`

Selected for:

- AP event envelope parsing.
- WebSocket JSON messages.
- JSONL accepted event record writing.
- Test fixture construction.

Boundary rule:

- `src/protocol` may expose JSON parse/serialize functions.
- `src/storage` may use JSON to write JSONL records.
- `src/server` may use JSON to serialize HTTP responses and WebSocket messages.
- `src/core` must not depend on `nlohmann::json`.

Implementation rule:

- Protocol code parses inbound JSON into project DTO/domain input types before calling runtime/core.
- Storage code serializes accepted records after runtime acceptance.
- Tests may use `nlohmann::json` fixtures even when the tested core code itself has no JSON
  dependency.

Rejected alternatives:

- RapidJSON: faster, but more verbose and less ergonomic for the first AP milestone.
- Boost.JSON: works well with Boost, but `nlohmann/json` is simpler for DTO-heavy early work and
  easier for interns to read.
- Manual JSON parsing: rejected because protocol validation should not depend on ad hoc string
  handling.

### HTTP/WebSocket: Boost.Beast + Boost.Asio

Selected for:

- `POST /api/v0/events`.
- `GET /api/v0/battles/{battle_id}/snapshot`.
- `WS /api/v0/live`.

Boundary rule:

- `src/server` owns all Beast/Asio types.
- `src/ap_runtime` exposes plain C++ interfaces for event submission and live update sinks.
- `src/server` adapts transport messages into protocol/runtime calls.

Initial threading model:

- V0 server may start with one Asio `io_context` thread.
- A later RFD may add a thread pool if latency or concurrent client count requires it.

Implementation rule:

- HTTP handlers should validate request shape, call protocol parsing, submit to `ApRuntime`, and map
  `Result<T>` errors to HTTP status codes.
- WebSocket sessions should subscribe to runtime live updates and write already-accepted event or
  snapshot messages.
- Storage must happen before WebSocket broadcast, as defined by the AP V0 event-loop RFD.

Rejected alternatives:

- Crow: convenient, but it is a higher-level framework that can encourage placing AP behavior in
  route handlers.
- Drogon: capable, but heavier than the AP V0 needs.
- websocketpp plus a separate HTTP library: splits transport dependencies and increases integration
  work.
- Raw sockets: rejected because HTTP/WebSocket correctness should not be hand-rolled.

### Result/Error: project-local `Result<T>`

Selected shape:

```cpp
namespace fenghuo {

enum class ErrorCode {
    InvalidArgument,
    ParseError,
    UnsupportedSchema,
    UnknownEventType,
    MissingField,
    InvalidPayload,
    DuplicateEvent,
    Conflict,
    NotFound,
    StorageFailure,
    TransportFailure,
    Internal,
};

struct Error {
    ErrorCode code;
    std::string message;
};

template <typename T>
class Result;

template <>
class Result<void>;

} // namespace fenghuo
```

Requirements:

- C++20 compatible.
- Header-only project-local implementation.
- No exceptions required for normal control flow.
- Must carry structured `ErrorCode` and human-readable message.
- Must support `Result<void>`.
- Must make success/failure state explicit at call sites.
- Must not allocate on successful `void` results.

Boundary rule:

- Use `Result<T>` for recoverable errors from `src/core`, `src/protocol`, `src/storage`,
  `src/ap_runtime`, and `src/server`.
- Do not throw exceptions across module boundaries for expected validation/runtime failures.
- Exceptions from third-party libraries must be caught at the module boundary and converted to
  `Result<T>`.

Initial API:

- `Result<T>::ok(T)` for success construction.
- `Result<T>::err(Error)` for failure construction.
- `T& value()` / `const T& value() const` accessors for success values.
- `const Error& error() const` accessor for failures.
- `bool has_value() const` for explicit state checks.
- `explicit operator bool() const` for guard-style call sites.
- `Result<void>` success and failure construction with the same call-site style.
- Misuse of `value()` on errors or `error()` on successes throws `BadResultAccess`. This is for
  programmer error, not normal control flow.

Rejected alternatives:

- `std::expected`: not available in C++20.
- `tl::expected`: good option, but adds another dependency before the project needs it.
- Boost.Outcome: powerful, but heavier than the current error model.
- Return `bool` plus output parameters: loses structured error information and makes tests weaker.

### Build dependencies

The development container should install system packages for:

- `nlohmann-json3-dev`
- `libboost-system-dev`
- `libboost-thread-dev`

CMake should eventually expose imported/link targets for these dependencies when implementation
starts. This RFD does not require code to include these libraries immediately.

## Drawbacks

- Boost headers can increase compile time.
- `nlohmann/json` is not the fastest JSON library.
- A project-local `Result<T>` creates a small maintenance burden.
- Choosing Beast means the team must understand async ownership and lifetimes rather than relying on
  a web framework.

## Rationale and alternatives

The chosen set keeps dependencies understandable and module boundaries explicit. `nlohmann/json`
keeps protocol work readable, Beast/Asio keeps server transport capable without a full framework, and
a local `Result<T>` avoids requiring C++23 or adding a dependency just for expected-style errors.

The project can revisit any choice after AP V0 if measured build time, runtime latency, or code
complexity becomes a blocker.

## Prior art

- Boost.Asio/Beast are common choices for C++ network services that need HTTP and WebSocket without a
  full framework.
- `nlohmann/json` is widely used for readable JSON DTO handling.
- `embedev` favors narrow module-owned interfaces over framework-owned business logic.

## Unresolved questions

- Exact JSON schema files are not yet defined.
- Exact WebSocket message envelope is still owned by `apps/rfd/0002-ap-v0-event-loop.md`.

## Validation and rollout

- Unit:
  - `Result<T>` success/failure behavior and `Result<void>`.
  - JSON parsing and serialization once protocol DTOs exist.
  - Beast server handler tests once server is introduced.
- Module-local integration:
  - `src/protocol` parses AP event JSON through `nlohmann/json`.
  - `src/server` converts Beast HTTP/WebSocket messages into protocol/runtime calls.
- Cross-module integration:
  - AP V0 sequence through HTTP POST and WebSocket after Phase 5.
  - `base/container/podman/cli/fenghuo.sh run just test`.
- Migration or rollout:
  - install selected development packages in the container.
  - introduce project-local `Result<T>` before core/protocol implementation.
  - keep Beast and `nlohmann/json` out of `src/core`.
- Completed validation:
  - `base/container/podman/cli/fenghuo.sh build`
  - `base/container/podman/cli/fenghuo.sh run bash -lc '<compile check including boost/asio.hpp, boost/beast.hpp, nlohmann/json.hpp>'`
  - `base/container/podman/cli/fenghuo.sh run just test`
  - `fenghuo.result` covers `Result<T>` success/failure behavior, move-only values, `Result<void>`,
    and bad-access exceptions.
- Deferred gaps:
  - CMake does not yet expose dedicated dependency targets because no implementation target consumes
    JSON or Beast yet.
  - `just test` currently reports no tests because AP modules have not been implemented.
  - performance comparison with RapidJSON or Boost.JSON.
  - server thread-pool design.
  - TLS support.

## Decision log

- 2026-05-19: Draft created; selected `nlohmann/json`, Boost.Beast/Asio, and project-local
  `Result<T>`.
- 2026-05-19: Accepted for AP V0. Container image builds with `nlohmann-json3-dev`,
  `libboost-system-dev`, and `libboost-thread-dev`; a compile/link smoke check for JSON, Beast, and
  Asio passed; `base/container/podman/cli/fenghuo.sh run just test` passed with no tests discovered.
- 2026-05-19: Implemented project-local `Result<T>`, `Result<void>`, `Error`, `ErrorCode`, and
  `BadResultAccess` in `src/fenghuo/result.hpp`; added `fenghuo.result` CTest coverage.

## Future possibilities

- Replace local `Result<T>` with `std::expected` if the project moves to C++23.
- Add JSON schema generation and validation tooling.
- Add a server framework only if Beast/Asio transport code becomes a maintenance bottleneck.
