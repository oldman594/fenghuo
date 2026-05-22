#!/usr/bin/env bash
set -euo pipefail

base_url="${FENGHUO_AP_URL:-http://127.0.0.1:8080}"
room_id="${FENGHUO_ROOM_ID:-demo-room-001}"
battle_id="${FENGHUO_BATTLE_ID:-demo-room-battle-001}"
source_id="${FENGHUO_SOURCE_ID:-demo-room-ui}"
occurred_at_ms="${FENGHUO_OCCURRED_AT_MS:-1730001000000}"

usage() {
  cat <<'EOF'
usage: tools/demo/room_v1_sequence.sh [BASE_URL]

Environment:
  FENGHUO_AP_URL          Default AP base URL. Defaults to http://127.0.0.1:8080.
  FENGHUO_ROOM_ID         Room id used in generated commands. Defaults to demo-room-001.
  FENGHUO_BATTLE_ID       Linked battle id used when starting. Defaults to demo-room-battle-001.
  FENGHUO_SOURCE_ID       Source id used in generated commands. Defaults to demo-room-ui.
  FENGHUO_OCCURRED_AT_MS  Base occurred_at_ms timestamp. Defaults to 1730001000000.
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
  curl --fail-with-body --silent --show-error \
    "${base_url}${path}"
  printf '\n' >&2
}

event_base() {
  local event_id="$1"
  local sequence="$2"
  local timestamp=$((occurred_at_ms + sequence))
  printf '"event_id":"%s","source_id":"%s","sequence":%s,"occurred_at_ms":%s' \
    "${event_id}" "${source_id}" "${sequence}" "${timestamp}"
}

post_json "create room ${room_id}" "/api/v0/rooms" "{
  $(event_base demo-room-evt-001 1),
  \"room_id\": \"${room_id}\",
  \"room_code\": \"483921\",
  \"name\": \"Demo room\",
  \"mode\": \"team_deathmatch\",
  \"max_players\": 2,
  \"teams\": [
    {\"team_id\": \"red\", \"display_name\": \"Red\", \"max_players\": 1},
    {\"team_id\": \"blue\", \"display_name\": \"Blue\", \"max_players\": 1}
  ]
}"

post_json "join red player" "/api/v0/rooms/${room_id}/players" "{
  $(event_base demo-room-evt-002 2),
  \"player_id\": \"p-red-01\",
  \"display_name\": \"Red 01\",
  \"team_id\": \"red\",
  \"module_id\": \"module-red-01\"
}"

post_json "join blue player" "/api/v0/rooms/${room_id}/players" "{
  $(event_base demo-room-evt-003 3),
  \"player_id\": \"p-blue-01\",
  \"display_name\": \"Blue 01\",
  \"team_id\": \"blue\",
  \"module_id\": \"module-blue-01\"
}"

post_json "ready red player" "/api/v0/rooms/${room_id}/players/p-red-01/ready" "{
  $(event_base demo-room-evt-004 4),
  \"ready\": true
}"

post_json "ready blue player" "/api/v0/rooms/${room_id}/players/p-blue-01/ready" "{
  $(event_base demo-room-evt-005 5),
  \"ready\": true
}"

post_json "start room" "/api/v0/rooms/${room_id}/start" "{
  $(event_base demo-room-evt-006 6),
  \"battle_id\": \"${battle_id}\",
  \"duration_ms\": 600000
}"

get_json "room snapshot ${room_id}" "/api/v0/rooms/${room_id}"

printf 'GET linked battle snapshot %s\n' "${battle_id}" >&2
if ! curl --fail-with-body --silent --show-error \
  "${base_url}/api/v0/battles/${battle_id}/snapshot"; then
  printf '\nlinked battle snapshot is not available until room-to-battle event orchestration is implemented.\n' >&2
fi
printf '\n'
