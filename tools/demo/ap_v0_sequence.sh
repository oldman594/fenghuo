#!/usr/bin/env bash
set -euo pipefail

base_url="${FENGHUO_AP_URL:-http://127.0.0.1:8080}"
battle_id="${FENGHUO_BATTLE_ID:-demo-battle-001}"
source_id="${FENGHUO_SOURCE_ID:-demo-sim}"
occurred_at_ms="${FENGHUO_OCCURRED_AT_MS:-1730000000000}"

usage() {
  cat <<'EOF'
usage: tools/demo/ap_v0_sequence.sh [BASE_URL]

Environment:
  FENGHUO_AP_URL          Default AP base URL. Defaults to http://127.0.0.1:8080.
  FENGHUO_BATTLE_ID       Battle id used in generated events. Defaults to demo-battle-001.
  FENGHUO_SOURCE_ID       Source id used in generated events. Defaults to demo-sim.
  FENGHUO_OCCURRED_AT_MS  Base occurred_at_ms timestamp. Defaults to 1730000000000.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ $# -gt 0 ]]; then
  base_url="$1"
fi

post_event() {
  local event_id="$1"
  local event_type="$2"
  local sequence="$3"
  local payload="$4"
  local timestamp=$((occurred_at_ms + sequence))

  printf 'POST %s %s\n' "${event_type}" "${event_id}" >&2
  curl --fail-with-body --silent --show-error \
    --request POST \
    --header 'content-type: application/json' \
    --data-binary @- \
    "${base_url}/api/v0/events" <<EOF
{
  "schema_version": 0,
  "event_id": "${event_id}",
  "event_type": "${event_type}",
  "battle_id": "${battle_id}",
  "source_id": "${source_id}",
  "sequence": ${sequence},
  "occurred_at_ms": ${timestamp},
  "payload": ${payload}
}
EOF
  printf '\n' >&2
}

post_event demo-evt-001 player_joined 1 '{
  "player_id": "p-red-01",
  "display_name": "Red 01",
  "team_id": "red",
  "module_id": "module-red-01"
}'

post_event demo-evt-002 player_joined 2 '{
  "player_id": "p-blue-01",
  "display_name": "Blue 01",
  "team_id": "blue",
  "module_id": "module-blue-01"
}'

post_event demo-evt-003 battle_started 3 '{
  "mode": "team_deathmatch",
  "duration_ms": 600000
}'

post_event demo-evt-004 shot 4 '{
  "player_id": "p-red-01",
  "weapon_id": "rifle-01",
  "ammo_after": 29
}'

post_event demo-evt-005 hit 5 '{
  "attacker_player_id": "p-red-01",
  "target_player_id": "p-blue-01",
  "weapon_id": "rifle-01",
  "damage": 10,
  "hit_zone": "torso"
}'

post_event demo-evt-006 battle_ended 6 '{
  "reason": "time_limit"
}'

printf 'GET snapshot %s\n' "${battle_id}" >&2
curl --fail-with-body --silent --show-error \
  "${base_url}/api/v0/battles/${battle_id}/snapshot"
printf '\n'
