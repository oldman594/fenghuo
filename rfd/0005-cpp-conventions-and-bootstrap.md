# RFD: C++ conventions and bootstrap

- Status: Draft
- Owners: fenghuo
- Created: 2026-05-19
- Updated: 2026-05-19

## Summary

Fenghuo uses C++20, CMake, `just`, and a Podman development container. The container mirrors host
user identity and mounts persistent host directories for build, cache, dependencies, and home state.

## Motivation

AP development should be repeatable and should not produce root-owned files on the host. The project
also needs basic C++ conventions before module implementation starts.

## Guide-level explanation

Build the image:

```bash
base/container/podman/cli/fenghuo.sh build
```

Enter the container:

```bash
base/container/podman/cli/fenghuo.sh shell
```

Run tests non-interactively:

```bash
base/container/podman/cli/fenghuo.sh run just test
```

Inside the container:

```bash
just build
just test
```

## Reference-level explanation

### Container mounts

The helper follows the same mounting model as `embedev`: Podman starts the container with root plus
`--userns=keep-id`, passes the host user name, uid, and gid into the container, and the container
bootstrap creates a matching runtime user. Files written under the repository mount are owned by the
same user on host and container.

Persistent host directories are mounted under:

```text
~/.local/share/fenghuo/
├── build      -> /work/build
├── cache      -> /work/.cache
├── ccache     -> /work/.ccache
├── deps       -> /work/deps
└── home-cache -> /home/<host-user>/.cache/fenghuo
```

The repository is mounted as `/work/fenghuo:rw,rslave`. Common developer configuration directories
such as `~/.config`, `~/.local`, `~/.ssh`, `.gitconfig`, `.git-credentials`, `.gnupg`, and `.netrc`
are mounted individually under the matching container home path.

Set `FENGHUO_DATA_ROOT` to place these directories elsewhere:

```bash
FENGHUO_DATA_ROOT=/data/fenghuo-dev base/container/podman/cli/fenghuo.sh shell
```

### C++ conventions

- Default language standard: C++20.
- Public APIs should use standard library types unless an RFD accepts another dependency.
- CMake is the expected C++ build system unless a later RFD chooses another one.
- `just` is the command entrypoint for build and test.
- `src/core` owns pure battle domain logic and must not depend on network, files, hardware, or wall
  clock APIs.
- `src/protocol` owns parsing, serialization, DTOs, and validation for external messages.
- `src/ap_runtime` owns session orchestration, deduplication, and state transitions.
- `src/server` owns HTTP/WebSocket transport and must not implement game rules.
- `src/storage` owns durable records and must not decide scoring.
- `src/simulator` owns host-side event generation and should use the same protocol boundary as real
  event ingress.

### API design

- Prefer explicit IDs such as `BattleId`, `PlayerId`, `TeamId`, and `EventId` over raw strings in
  domain APIs.
- Prefer `std::chrono::milliseconds` or stronger aliases for time values at boundaries.
- Return structured errors from protocol and runtime boundaries.
- Keep domain state updates deterministic from accepted events.

### Testing conventions

- Protocol tests must include valid, invalid, duplicate, and forward-compatibility cases.
- Core tests must verify scoring, health, lifecycle, and team rules without starting server code.
- Runtime tests must verify ordering, deduplication, storage failure behavior, and live snapshot
  fanout with fakes.
- Server tests must validate transport behavior without duplicating core battle-rule assertions.

## Drawbacks

- Container setup adds maintenance overhead.
- The selected Boost.Beast/Asio transport dependency can increase compile time.

## Rationale and alternatives

Podman container development wins because host tool availability varies and permission correctness
matters. Native builds remain possible when host tools are installed.

## Prior art

- `embedev` container-first bootstrap model.
- `embedev` `just` entrypoint convention.

## Unresolved questions

- Which C++ package/dependency manager, if any, should be introduced.

## Validation and rollout

- Unit: not applicable for bootstrap rules.
- Module-local integration: `base/container/podman/cli/fenghuo.sh run just test`.
- Cross-module integration: container build and test after dependency changes.
- Migration or rollout: keep CMake/just/container files in repo root and `base/container`.
- Deferred gaps: no tests exist yet beyond the minimal build target.

## Decision log

- 2026-05-19: Draft created from bootstrap and C++ convention documents.
- 2026-05-19: Updated the Podman mount model to match `embedev`: root container startup with
  `--userns=keep-id`, `--group-add keep-groups`, `:rw,rslave` mounts, explicit host state
  directories, and individual user config mounts instead of `:Z` relabels or a whole-home mount.
  Validation passed: container `id` reports `zhangkunjie 1005 zhangkunjie 1005`, a container-created
  repository file has the same host owner/group, and `base/container/podman/cli/fenghuo.sh run just
  test` builds successfully.

## Future possibilities

- Add dependency cache for a chosen package manager.
- Add lint/format commands once tools are selected.
