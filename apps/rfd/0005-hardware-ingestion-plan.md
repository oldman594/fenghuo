# RFD: Hardware ingestion plan

- Status: Draft
- Owners: apps
- Created: 2026-05-22
- Updated: 2026-05-22

## Summary

The current Fenghuo AP prototype has a complete minimal software loop for room setup and battle
simulation. The next milestone is to connect physical gun/module hardware to the AP so real fire and
hit signals can drive the existing battle runtime.

This RFD defines the remaining work needed for a simple hardware-backed battle system: device
binding, hardware event ingestion, idempotency, validation, test tools, and acceptance criteria.

## Current software baseline

Room flow is already available:

```text
create room
join room
assign teams
ready players
start room
room start creates battle
```

Battle flow is already available:

```text
player_joined
battle_started
shot
hit
health deduction
death check
team score
battle_paused
battle_resumed
battle_ended
WebSocket state updates
```

Manual testing surfaces are already available:

```text
/console
/sim
tools/demo/*.sh
```

This means the first hardware integration should not redesign rooms or battle rules. It should
translate hardware signals into the existing AP event model.

## Goals

- Let hardware report fire and hit signals to the AP.
- Bind each physical device to one logical player in a room or battle.
- Convert hardware input into existing `shot` and `hit` battle events.
- Keep hardware retries safe by preserving event idempotency.
- Keep the first version simple enough to test with curl, scripts, and the existing web console.
- Preserve the current simulator so software can still be tested without hardware.

## Non-goals for the first hardware version

- Mobile app support.
- Complex anti-cheat logic.
- Friendly-fire gameplay.
- Real-time positioning.
- Weapon inventory.
- Per-body-part damage tables beyond passing `hit_zone`.
- Offline hardware sync across multiple APs.
- Firmware protocol optimization.

## Hardware-to-AP event contract

The AP already accepts battle events through:

```text
POST /api/v0/events
```

The minimal hardware integration can submit the same event format.

### `shot`

Used when a player fires.

```json
{
  "schema_version": 0,
  "event_type": "shot",
  "battle_id": "battle-001",
  "event_id": "hw-shot-0001",
  "source_id": "device-red-01",
  "sequence": 1001,
  "occurred_at_ms": 1730000000000,
  "payload": {
    "player_id": "p-red-01",
    "weapon_id": "rifle-01",
    "ammo_after": 29
  }
}
```

### `hit`

Used when one player hits another player. This is the event that affects health.

```json
{
  "schema_version": 0,
  "event_type": "hit",
  "battle_id": "battle-001",
  "event_id": "hw-hit-0001",
  "source_id": "device-red-01",
  "sequence": 1002,
  "occurred_at_ms": 1730000000050,
  "payload": {
    "attacker_player_id": "p-red-01",
    "target_player_id": "p-blue-01",
    "weapon_id": "rifle-01",
    "damage": 10,
    "hit_zone": "torso"
  }
}
```

## Required hardware fields

Every hardware event must provide:

```text
battle_id
event_id
source_id
sequence
occurred_at_ms
event_type
payload
```

For `shot`, payload must provide:

```text
player_id
weapon_id
ammo_after
```

For `hit`, payload must provide:

```text
attacker_player_id
target_player_id
weapon_id
damage
hit_zone
```

## Device binding

The system needs a stable mapping between physical devices and logical players.

Initial binding model:

```text
device_id -> module_id -> player_id
```

The current room player model already has:

```text
player_id
display_name
team_id
module_id
```

For the first version, `module_id` should be treated as the hardware identity used during room join.

Example:

```json
{
  "player_id": "p-red-01",
  "display_name": "Red 01",
  "team_id": "red",
  "module_id": "device-red-01"
}
```

Hardware can then use:

```text
source_id = device-red-01
```

The AP can later add a dedicated binding endpoint if manual room join is not enough.

## Proposed future binding endpoints

These endpoints are not required for the first curl-based hardware demo, but they are the likely
next step once physical devices are available.

```text
POST /api/v0/rooms/{room_id}/devices/bind
GET  /api/v0/rooms/{room_id}/devices
POST /api/v0/rooms/{room_id}/devices/{device_id}/unbind
```

Example bind request:

```json
{
  "device_id": "device-red-01",
  "player_id": "p-red-01"
}
```

## Idempotency and retries

Hardware must assume network delivery can fail. The AP already deduplicates accepted battle events by
`source_id` and `sequence`.

Hardware retry rule:

```text
If the same physical action is retried, reuse the same source_id and sequence.
```

Do not generate a new sequence for a retry of the same hit. Otherwise the AP will treat it as a new
event and may deduct health twice.

Recommended hardware sequence behavior:

```text
one device has one monotonically increasing sequence counter
sequence increases only for a new physical action
retry keeps the same sequence
```

## Friendly-fire policy

V1 should not support friendly fire as gameplay.

Recommended V1 behavior:

```text
If attacker and target are on the same team, reject or ignore the hit.
```

The current simulator avoids same-team targets. Hardware ingestion should make the same rule explicit
so a bad hardware signal cannot damage a teammate.

## Backend work items

1. Document the hardware event contract.
2. Confirm current `POST /api/v0/events` accepts hardware-shaped `shot` and `hit`.
3. Add tests for hardware-style event IDs, source IDs, and retry behavior.
4. Add friendly-fire rejection or ignore behavior if it is not already enforced.
5. Add a hardware demo script under `tools/demo/`.
6. Add a console view field that shows each player's `module_id`.
7. Add a small hardware debug log panel or endpoint if needed.
8. Add dedicated device binding endpoints only after the hardware workflow is proven.

## Hardware work items

1. Choose the stable device identity format, for example `device-red-01`.
2. Make every device maintain a persistent or session-local sequence counter.
3. Send `shot` when trigger/fire is detected.
4. Send `hit` when a valid target hit is detected.
5. Include attacker and target identity in the hit signal.
6. Retry failed HTTP submissions without changing `source_id` or `sequence`.
7. Provide a simple hardware debug mode that prints outgoing event JSON.

## Demo script plan

Add:

```text
tools/demo/hardware_v0_sequence.sh
```

The script should:

1. Create a room.
2. Join two players with hardware-like `module_id` values.
3. Ready both players.
4. Start the room.
5. Submit a `shot` from `device-red-01`.
6. Submit a `hit` from `device-red-01` against `p-blue-01`.
7. Fetch the battle snapshot.
8. Verify blue player's health decreased.
9. Retry the same `hit` with the same `source_id` and `sequence`.
10. Verify health is not deducted twice.

## Manual test flow

1. Start the AP:

```bash
./build/fenghuo-apd --port 8080 --event-log-root /tmp/fenghuo-ap-events-dev
```

2. Open:

```text
http://127.0.0.1:8080/console
```

3. Create a room and join players using hardware-like `module_id` values.
4. Start the room.
5. Submit hardware-style `shot` and `hit` requests with curl or a demo script.
6. Watch the console update over WebSocket.
7. Confirm health, alive state, and team score are correct.

## Acceptance criteria

The first hardware-backed milestone is accepted when:

- A physical or hardware-emulator device can submit `shot`.
- A physical or hardware-emulator device can submit `hit`.
- A hit decreases the target player's health.
- Repeating the same hardware event does not deduct health twice.
- Same-team hits do not damage teammates.
- The web console shows the updated battle snapshot without refresh.
- The battle can still be paused, resumed, and ended from the console.
- The existing simulator still works.
- `just test` passes.

## Suggested implementation order

1. Add backend tests for hardware-shaped `shot` and `hit` events.
2. Add friendly-fire rejection or ignore behavior.
3. Add `tools/demo/hardware_v0_sequence.sh`.
4. Test the demo script against `/console`.
5. Connect one hardware emulator or simple firmware client.
6. Decide whether dedicated device binding endpoints are needed.
7. Add binding endpoints and UI only if manual `module_id` binding is not enough.

## Open questions

- Does the target module know the attacker's identity, or does the AP need to correlate shot and hit
  signals?
- Will hardware communicate over HTTP directly, or through a gateway process?
- Should `sequence` persist across device reboot, or reset per battle/session?
- Should AP assign `damage`, or should hardware provide it?
- Should the AP reject friendly fire with an error, or accept and ignore it as a no-op?
- Does hardware need an acknowledgement format beyond the current HTTP response body?
