#!/usr/bin/env bash
set -euo pipefail

base_url="${FENGHUO_AP_URL:-http://127.0.0.1:8080}"
room_id="${FENGHUO_ROOM_ID:-hardware-room-001}"
battle_id="${FENGHUO_BATTLE_ID:-hardware-battle-001}"
source_id="${FENGHUO_SOURCE_ID:-hardware-demo}"
occurred_at_ms="${FENGHUO_OCCURRED_AT_MS:-1730002000000}"

usage() {
  cat <<'EOF'
usage: tools/demo/hardware_v0_sequence.sh [BASE_URL]

Environment:
  FENGHUO_AP_URL          Default AP base URL. Defaults to http://127.0.0.1:8080.
  FENGHUO_ROOM_ID         Room id used in generated commands. Defaults to hardware-room-001.
  FENGHUO_BATTLE_ID       Battle id used when starting the room. Defaults to hardware-battle-001.
  FENGHUO_SOURCE_ID       Source id used in generated room commands. Defaults to hardware-demo.
  FENGHUO_OCCURRED_AT_MS  Base occurred_at_ms timestamp. Defaults to 1730002000000.
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ $# -gt 0 ]]; then
  base_url="$1"
fi

post_json() {
  local label="$1"
  local path="$2"
  local body="$3"

  printf 'POST %s\n' "${label}" >&2
  curl --fail-with-body --silent --show-error \
    --request POST \
    --header 'content-type: application/json' \
    --data-binary "${body}" \
    "${base_url}${path}"
  printf '\n' >&2
}

get_json() {
  local label="$1"
  local path="$2"

  printf 'GET %s\n' "${label}" >&2
  curl --fail-with-body --silent --show-error "${base_url}${path}"
  printf '\n' >&2
}

event_base() {
  local event_id="$1"
  local sequence="$2"
  local timestamp=$((occurred_at_ms + sequence))
  printf '"event_id":"%s","source_id":"%s","sequence":%s,"occurred_at_ms":%s' \
    "${event_id}" "${source_id}" "${sequence}" "${timestamp}"
}

post_battle_event() {
  local event_id="$1"
  local event_type="$2"
  local source="$3"
  local sequence="$4"
  local payload="$5"
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
  "source_id": "${source}",
  "sequence": ${sequence},
  "occurred_at_ms": ${timestamp},
  "payload": ${payload}
}
EOF
  printf '\n' >&2
}

post_json "create room ${room_id}" "/api/v0/rooms" "{
  $(event_base hardware-room-evt-001 1),
  \"room_id\": \"${room_id}\",
  \"room_code\": \"778899\",
  \"name\": \"Hardware demo room\",
  \"mode\": \"team_deathmatch\",
  \"max_players\": 2,
  \"teams\": [
    {\"team_id\": \"red\", \"display_name\": \"Red\", \"max_players\": 1},
    {\"team_id\": \"blue\", \"display_name\": \"Blue\", \"max_players\": 1}
  ]
}"

post_json "join red player" "/api/v0/rooms/${room_id}/players" "{
  $(event_base hardware-room-evt-002 2),
  \"player_id\": \"p-red-01\",
  \"display_name\": \"Red 01\",
  \"team_id\": \"red\",
  \"module_id\": \"module-red-01\"
}"

post_json "join blue player" "/api/v0/rooms/${room_id}/players" "{
  $(event_base hardware-room-evt-003 3),
  \"player_id\": \"p-blue-01\",
  \"display_name\": \"Blue 01\",
  \"team_id\": \"blue\",
  \"module_id\": \"module-blue-01\"
}"

post_json "register red headset" "/api/v0/rooms/${room_id}/devices" "{
  $(event_base hardware-room-evt-004 4),
  \"device_id\": \"device-head-red-01\",
  \"device_kind\": \"headset_receiver\",
  \"display_name\": \"Red Headset\",
  \"battery_percent\": 95,
  \"signal_strength\": 88
}"

post_json "register blue headset" "/api/v0/rooms/${room_id}/devices" "{
  $(event_base hardware-room-evt-005 5),
  \"device_id\": \"device-head-blue-01\",
  \"device_kind\": \"headset_receiver\",
  \"display_name\": \"Blue Headset\",
  \"battery_percent\": 93,
  \"signal_strength\": 86
}"

post_json "bind red headset" "/api/v0/rooms/${room_id}/devices/device-head-red-01/bind" "{
  $(event_base hardware-room-evt-006 6),
  \"player_id\": \"p-red-01\"
}"

post_json "bind blue headset" "/api/v0/rooms/${room_id}/devices/device-head-blue-01/bind" "{
  $(event_base hardware-room-evt-007 7),
  \"player_id\": \"p-blue-01\"
}"

post_json "heartbeat red headset" "/api/v0/rooms/${room_id}/devices/device-head-red-01/heartbeat" "{
  $(event_base hardware-room-evt-008 8),
  \"battery_percent\": 92,
  \"signal_strength\": 81,
  \"online\": true
}"

post_json "heartbeat blue headset" "/api/v0/rooms/${room_id}/devices/device-head-blue-01/heartbeat" "{
  $(event_base hardware-room-evt-009 9),
  \"battery_percent\": 90,
  \"signal_strength\": 80,
  \"online\": true
}"

post_json "ready red player" "/api/v0/rooms/${room_id}/players/p-red-01/ready" "{
  $(event_base hardware-room-evt-010 10),
  \"ready\": true
}"

post_json "ready blue player" "/api/v0/rooms/${room_id}/players/p-blue-01/ready" "{
  $(event_base hardware-room-evt-011 11),
  \"ready\": true
}"

post_json "start room" "/api/v0/rooms/${room_id}/start" "{
  $(event_base hardware-room-evt-012 12),
  \"battle_id\": \"${battle_id}\",
  \"duration_ms\": 600000
}"

get_json "room devices ${room_id}" "/api/v0/rooms/${room_id}/devices"

post_battle_event hardware-battle-evt-001 shot device-gun-red-01 1 '{
  "player_id": "p-red-01",
  "weapon_id": "rifle-01",
  "ammo_after": 29
}'

post_battle_event hardware-battle-evt-002 hit device-gun-red-01 2 '{
  "attacker_player_id": "p-red-01",
  "target_player_id": "p-blue-01",
  "weapon_id": "rifle-01",
  "damage": 10,
  "hit_zone": "torso"
}'

get_json "battle snapshot ${battle_id}" "/api/v0/battles/${battle_id}/snapshot"
