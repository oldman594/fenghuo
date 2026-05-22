# Minimal AP Runbook

This document runs the current AP V0 server, submits a demo battle sequence, reads the final
snapshot, and inspects the JSONL accepted event log.

## Build

Build and test inside the Podman development container:

```bash
base/container/podman/cli/fenghuo.sh build
base/container/podman/cli/fenghuo.sh run just test
```

If the host already has the required C++ tools and dependencies installed, the same build entrypoint
works directly:

```bash
just test
```

## Start AP

Start `fenghuo-apd` from the container:

```bash
base/container/podman/cli/fenghuo.sh run ./build/fenghuo-apd \
  --port 8080 \
  --event-log-root /tmp/fenghuo-ap-events
```

The server listens on `0.0.0.0:<port>` and exposes:

```text
POST /api/v0/events
GET  /api/v0/battles/{battle_id}/snapshot
WS   /api/v0/live
```

Stop it with `Ctrl-C` or `SIGTERM`. On startup, `fenghuo-apd` replays accepted JSONL records from
`--event-log-root` before accepting new traffic.

## Submit Demo Events

In another shell, submit the V0 demo sequence:

```bash
tools/demo/ap_v0_sequence.sh http://127.0.0.1:8080
```

The script posts:

```text
player_joined
player_joined
battle_started
shot
hit
battle_ended
```

It then prints the final snapshot response for `demo-battle-001`.

To choose another battle id:

```bash
FENGHUO_BATTLE_ID=my-battle-001 tools/demo/ap_v0_sequence.sh http://127.0.0.1:8080
```

## Read Snapshot Manually

```bash
curl --fail-with-body --silent --show-error \
  http://127.0.0.1:8080/api/v0/battles/demo-battle-001/snapshot
```

Expected snapshot facts after the demo sequence:

- battle phase is `ended`
- `p-blue-01` health is `90`
- red team score is `1`
- latest accepted event id is `demo-evt-006`

## Watch WebSocket

Use any WebSocket client and connect to:

```text
ws://127.0.0.1:8080/api/v0/live
```

Then run the demo script in another shell. Each accepted event publishes an `accepted_event` message
with the original event envelope and current snapshot.

One common command-line client is `websocat`:

```bash
websocat ws://127.0.0.1:8080/api/v0/live
```

`websocat` is not part of the Fenghuo development container.

## Inspect JSONL

Accepted events are appended to:

```text
<event-log-root>/<battle_id>.jsonl
```

For the run command above:

```bash
wc -l /tmp/fenghuo-ap-events/demo-battle-001.jsonl
tail -n 1 /tmp/fenghuo-ap-events/demo-battle-001.jsonl
```

Rejected events and exact duplicates are not appended to the accepted event log.

## Replay Check

To verify replay manually:

1. Run the demo sequence once.
2. Stop `fenghuo-apd`.
3. Start `fenghuo-apd` again with the same `--event-log-root`.
4. Query the snapshot endpoint.

The snapshot should still show the ended battle. The startup log prints the number of replayed
accepted events.

## Current Limits

- No authentication or authorization.
- No physical module transport.
- No bounded WebSocket backpressure queue.
- No JSONL compaction or indexed historical query.
