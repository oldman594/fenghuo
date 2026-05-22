const state = {
  rooms: [],
  selectedRoomId: "",
  battles: new Map(),
  eventSequence: Date.now(),
  sourceId: sessionStorage.getItem("fenghuo_console_source_id") || `console-${Date.now()}-${Math.random().toString(16).slice(2)}`,
};

sessionStorage.setItem("fenghuo_console_source_id", state.sourceId);

const els = {
  connectionStatus: document.querySelector("#connectionStatus"),
  refreshRooms: document.querySelector("#refreshRooms"),
  createRoomForm: document.querySelector("#createRoomForm"),
  roomList: document.querySelector("#roomList"),
  selectedRoomLabel: document.querySelector("#selectedRoomLabel"),
  roomDetail: document.querySelector("#roomDetail"),
  battleLabel: document.querySelector("#battleLabel"),
  battleDetail: document.querySelector("#battleDetail"),
  attackerSelect: document.querySelector("#attackerSelect"),
  targetSelect: document.querySelector("#targetSelect"),
  damageInput: document.querySelector("#damageInput"),
  joinRed: document.querySelector("#joinRed"),
  joinBlue: document.querySelector("#joinBlue"),
  readyAll: document.querySelector("#readyAll"),
  startRoom: document.querySelector("#startRoom"),
  closeRoom: document.querySelector("#closeRoom"),
  attackButton: document.querySelector("#attackButton"),
  pauseBattle: document.querySelector("#pauseBattle"),
  resumeBattle: document.querySelector("#resumeBattle"),
  endBattle: document.querySelector("#endBattle"),
  clearLog: document.querySelector("#clearLog"),
  eventLog: document.querySelector("#eventLog"),
};

function nowMs() {
  return Date.now();
}

function nextEvent(prefix) {
  const sequence = state.eventSequence++;
  return {
    event_id: `${prefix}-${nowMs()}-${sequence}`,
    source_id: state.sourceId,
    sequence,
    occurred_at_ms: nowMs(),
  };
}

function log(label, value) {
  const text = typeof value === "string" ? value : JSON.stringify(value, null, 2);
  const stamp = new Date().toLocaleTimeString();
  els.eventLog.textContent = `[${stamp}] ${label}\n${text}\n\n${els.eventLog.textContent}`;
}

async function requestJson(path, options = {}) {
  const response = await fetch(path, {
    headers: { "content-type": "application/json" },
    ...options,
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    const message = body.error?.message || response.statusText;
    throw new Error(`${response.status} ${message}`);
  }
  return body;
}

function selectedRoom() {
  return state.rooms.find((room) => room.room_id === state.selectedRoomId) || null;
}

function selectedBattle() {
  const room = selectedRoom();
  return room?.battle_id ? state.battles.get(room.battle_id) || null : null;
}

function roomPlayers(room) {
  return Object.values(room?.players || {});
}

function battlePlayers(battle) {
  return Object.values(battle?.players || {});
}

function renderRooms() {
  els.roomList.innerHTML = "";
  if (state.rooms.length === 0) {
    const empty = document.createElement("div");
    empty.className = "muted";
    empty.textContent = "No rooms yet.";
    els.roomList.append(empty);
    return;
  }

  for (const room of state.rooms) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `roomItem ${room.room_id === state.selectedRoomId ? "active" : ""}`;
    button.innerHTML = `
      <span class="roomTitle">
        <span>${escapeHtml(room.name || room.room_id)}</span>
        <span class="badge ${escapeHtml(room.phase)}">${escapeHtml(room.phase)}</span>
      </span>
      <span class="muted">${escapeHtml(room.room_id)} · ${roomPlayers(room).length}/${room.max_players}</span>
    `;
    button.addEventListener("click", () => {
      state.selectedRoomId = room.room_id;
      render();
    });
    els.roomList.append(button);
  }
}

function renderSelectedRoom() {
  const room = selectedRoom();
  els.selectedRoomLabel.textContent = room ? room.room_id : "None";
  const canCommand = Boolean(room && room.phase === "open");
  els.joinRed.disabled = !canCommand;
  els.joinBlue.disabled = !canCommand;
  els.readyAll.disabled = !canCommand || roomPlayers(room).length === 0;
  els.startRoom.disabled = !canCommand || roomPlayers(room).length === 0;
  els.closeRoom.disabled = !(room && (room.phase === "open" || room.phase === "ended"));

  if (!room) {
    els.roomDetail.className = "roomDetail empty";
    els.roomDetail.textContent = "Select or create a room.";
    return;
  }

  els.roomDetail.className = "roomDetail";
  const players = roomPlayers(room);
  const rows = players.map((player) => `
    <tr>
      <td>${escapeHtml(player.display_name)}</td>
      <td class="team-${escapeHtml(player.team_id)}">${escapeHtml(player.team_id)}</td>
      <td>${player.ready ? "ready" : "not ready"}</td>
      <td>${escapeHtml(player.module_id || "")}</td>
    </tr>
  `).join("");

  els.roomDetail.innerHTML = `
    <div class="summaryGrid">
      <div class="summaryCell"><span>Phase</span><strong>${escapeHtml(room.phase)}</strong></div>
      <div class="summaryCell"><span>Mode</span><strong>${escapeHtml(room.mode)}</strong></div>
      <div class="summaryCell"><span>Players</span><strong>${players.length}/${room.max_players}</strong></div>
      <div class="summaryCell"><span>Battle</span><strong>${escapeHtml(room.battle_id || "none")}</strong></div>
    </div>
    <table class="playerTable">
      <thead>
        <tr><th>Player</th><th>Team</th><th>Ready</th><th>Module</th></tr>
      </thead>
      <tbody>${rows || `<tr><td colspan="4" class="muted">No players.</td></tr>`}</tbody>
    </table>
  `;
}

function renderBattle() {
  const room = selectedRoom();
  const battle = selectedBattle();
  els.battleLabel.textContent = room?.battle_id || "None";

  const players = battlePlayers(battle);
  els.attackerSelect.innerHTML = "";
  els.targetSelect.innerHTML = "";
  for (const player of players.filter((item) => item.alive)) {
    const option = new Option(`${player.display_name} (${player.team_id})`, player.player_id);
    els.attackerSelect.add(option);
  }
  renderTargetOptions();

  const canAttack = Boolean(battle && battle.phase === "active" && els.attackerSelect.value && els.targetSelect.value);
  els.attackButton.disabled = !canAttack;
  els.pauseBattle.disabled = !(battle && battle.phase === "active");
  els.resumeBattle.disabled = !(battle && battle.phase === "paused");
  els.endBattle.disabled = !(battle && (battle.phase === "active" || battle.phase === "paused"));

  if (!battle) {
    els.battleDetail.className = "roomDetail empty";
    els.battleDetail.textContent = "Start a room to create a battle.";
    return;
  }

  els.battleDetail.className = "roomDetail";
  const rows = players.map((player) => `
    <tr>
      <td>${escapeHtml(player.display_name)}</td>
      <td class="team-${escapeHtml(player.team_id)}">${escapeHtml(player.team_id)}</td>
      <td>${player.health}</td>
      <td>${player.alive ? "alive" : "down"}</td>
      <td>${player.shot_count}/${player.hit_count}</td>
    </tr>
  `).join("");
  els.battleDetail.innerHTML = `
    <div class="summaryGrid">
      <div class="summaryCell"><span>Phase</span><strong>${escapeHtml(battle.phase)}</strong></div>
      <div class="summaryCell"><span>Mode</span><strong>${escapeHtml(battle.mode)}</strong></div>
      <div class="summaryCell"><span>Red Score</span><strong>${battle.teams?.red?.score || 0}</strong></div>
      <div class="summaryCell"><span>Blue Score</span><strong>${battle.teams?.blue?.score || 0}</strong></div>
    </div>
    <table class="playerTable">
      <thead>
        <tr><th>Player</th><th>Team</th><th>HP</th><th>Status</th><th>Shot/Hit</th></tr>
      </thead>
      <tbody>${rows || `<tr><td colspan="5" class="muted">No battle players.</td></tr>`}</tbody>
    </table>
  `;
}

function renderTargetOptions() {
  const battle = selectedBattle();
  const attacker = battle?.players?.[els.attackerSelect.value];
  const currentTarget = els.targetSelect.value;
  els.targetSelect.innerHTML = "";
  if (!battle || !attacker) {
    return;
  }
  for (const player of battlePlayers(battle)) {
    if (!player.alive || player.player_id === attacker.player_id || player.team_id === attacker.team_id) {
      continue;
    }
    els.targetSelect.add(new Option(`${player.display_name} (${player.team_id})`, player.player_id));
  }
  if (currentTarget) {
    els.targetSelect.value = currentTarget;
  }
}

function render() {
  renderRooms();
  renderSelectedRoom();
  renderBattle();
}

async function refreshRooms() {
  const body = await requestJson("/api/v0/rooms");
  state.rooms = body.rooms || [];
  if (state.selectedRoomId && !state.rooms.some((room) => room.room_id === state.selectedRoomId)) {
    state.selectedRoomId = "";
  }
  if (!state.selectedRoomId && state.rooms.length > 0) {
    state.selectedRoomId = state.rooms[0].room_id;
  }
  await refreshKnownBattles();
  render();
}

async function refreshKnownBattles() {
  for (const room of state.rooms) {
    if (!room.battle_id || state.battles.has(room.battle_id)) {
      continue;
    }
    try {
      await refreshBattle(room.battle_id);
    } catch (error) {
      log("battle refresh skipped", `${room.battle_id}: ${error.message}`);
    }
  }
}

async function createRoom(event) {
  event.preventDefault();
  const form = new FormData(els.createRoomForm);
  const roomId = String(form.get("room_id") || "").trim();
  const maxPlayers = Number(form.get("max_players") || 2);
  const teamSize = Number(form.get("team_size") || 1);
  const body = {
    ...nextEvent("console-room-created"),
    room_id: roomId,
    room_code: roomId,
    name: String(form.get("name") || "Console room"),
    mode: String(form.get("mode") || "team_deathmatch"),
    max_players: maxPlayers,
    teams: [
      { team_id: "red", display_name: "Red", max_players: teamSize },
      { team_id: "blue", display_name: "Blue", max_players: teamSize },
    ],
  };
  const response = await requestJson("/api/v0/rooms", {
    method: "POST",
    body: JSON.stringify(body),
  });
  state.selectedRoomId = response.room.room_id;
  log("create room", response);
  await refreshRooms();
}

async function joinTeam(teamId) {
  const room = selectedRoom();
  if (!room) {
    return;
  }
  const existing = new Set(roomPlayers(room).map((player) => player.player_id));
  let index = roomPlayers(room).filter((player) => player.team_id === teamId).length + 1;
  while (existing.has(`p-${teamId}-${String(index).padStart(2, "0")}`)) {
    index += 1;
  }
  const playerId = `p-${teamId}-${String(index).padStart(2, "0")}`;
  const body = {
    ...nextEvent(`console-${teamId}-joined`),
    player_id: playerId,
    display_name: `${teamId === "red" ? "Red" : "Blue"} ${String(index).padStart(2, "0")}`,
    team_id: teamId,
    module_id: `module-${teamId}-${String(index).padStart(2, "0")}`,
  };
  const response = await requestJson(`/api/v0/rooms/${encodeURIComponent(room.room_id)}/players`, {
    method: "POST",
    body: JSON.stringify(body),
  });
  log(`join ${teamId}`, response);
  upsertRoom(response.room);
}

async function readyAll() {
  const room = selectedRoom();
  if (!room) {
    return;
  }
  for (const player of roomPlayers(room).filter((item) => !item.ready)) {
    const response = await requestJson(
      `/api/v0/rooms/${encodeURIComponent(room.room_id)}/players/${encodeURIComponent(player.player_id)}/ready`,
      {
        method: "POST",
        body: JSON.stringify({ ...nextEvent("console-ready"), ready: true }),
      },
    );
    log(`ready ${player.player_id}`, response);
    upsertRoom(response.room);
  }
}

async function startRoom() {
  const room = selectedRoom();
  if (!room) {
    return;
  }
  const body = {
    ...nextEvent("console-room-started"),
    battle_id: `${room.room_id}-battle`,
    duration_ms: 600000,
  };
  const response = await requestJson(`/api/v0/rooms/${encodeURIComponent(room.room_id)}/start`, {
    method: "POST",
    body: JSON.stringify(body),
  });
  log("start room", response);
  upsertRoom(response.room);
  if (response.battle_snapshot) {
    upsertBattle(response.battle_snapshot);
  }
}

async function closeRoom() {
  const room = selectedRoom();
  if (!room) {
    return;
  }
  const response = await requestJson(`/api/v0/rooms/${encodeURIComponent(room.room_id)}/close`, {
    method: "POST",
    body: JSON.stringify(nextEvent("console-room-closed")),
  });
  log("close room", response);
  upsertRoom(response.room);
}

async function refreshBattle(battleId) {
  const response = await requestJson(`/api/v0/battles/${encodeURIComponent(battleId)}/snapshot`);
  upsertBattle(response.snapshot);
  return response.snapshot;
}

async function postBattleEvent(eventType, battleId, payload) {
  const event = nextEvent(`console-${eventType}`);
  const response = await requestJson("/api/v0/events", {
    method: "POST",
    body: JSON.stringify({
      schema_version: 0,
      event_id: event.event_id,
      event_type: eventType,
      battle_id: battleId,
      source_id: event.source_id,
      sequence: event.sequence,
      occurred_at_ms: event.occurred_at_ms,
      payload,
    }),
  });
  log(eventType, response);
  if (response.snapshot) {
    upsertBattle(response.snapshot);
  }
  return response;
}

async function attack() {
  const battle = selectedBattle();
  if (!battle) {
    return;
  }
  const attackerId = els.attackerSelect.value;
  const targetId = els.targetSelect.value;
  const damage = Number(els.damageInput.value || 10);
  const attacker = battle.players[attackerId];
  const target = battle.players[targetId];
  if (!attacker || !target || attacker.team_id === target.team_id) {
    log("attack skipped", "Select one attacker and one enemy target.");
    return;
  }
  await postBattleEvent("shot", battle.battle_id, {
    player_id: attackerId,
    weapon_id: "console-rifle",
    ammo_after: Math.max(0, Number(attacker.ammo || 30) - 1),
  });
  await postBattleEvent("hit", battle.battle_id, {
    attacker_player_id: attackerId,
    target_player_id: targetId,
    weapon_id: "console-rifle",
    damage,
    hit_zone: "torso",
  });
}

async function endBattle() {
  const battle = selectedBattle();
  if (!battle || (battle.phase !== "active" && battle.phase !== "paused")) {
    return;
  }
  await postBattleEvent("battle_ended", battle.battle_id, {
    reason: "manual",
  });
}

async function pauseBattle() {
  const battle = selectedBattle();
  if (!battle || battle.phase !== "active") {
    return;
  }
  await postBattleEvent("battle_paused", battle.battle_id, {
    reason: "operator",
  });
}

async function resumeBattle() {
  const battle = selectedBattle();
  if (!battle || battle.phase !== "paused") {
    return;
  }
  await postBattleEvent("battle_resumed", battle.battle_id, {});
}

function upsertRoom(room) {
  const index = state.rooms.findIndex((item) => item.room_id === room.room_id);
  if (index >= 0) {
    state.rooms[index] = room;
  } else {
    state.rooms.unshift(room);
  }
  state.selectedRoomId = room.room_id;
  render();
}

function upsertBattle(snapshot) {
  state.battles.set(snapshot.battle_id, snapshot);
  render();
}

function connectWebSocket() {
  const scheme = window.location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${scheme}://${window.location.host}/api/v0/live`);

  ws.addEventListener("open", () => {
    els.connectionStatus.textContent = "Live connected";
  });

  ws.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    log("websocket", message);
    if (message.type === "room_updated" && message.room) {
      upsertRoom(message.room);
    }
    if (message.type === "accepted_event" && message.snapshot) {
      upsertBattle(message.snapshot);
    }
  });

  ws.addEventListener("close", () => {
    els.connectionStatus.textContent = "Live disconnected, retrying";
    setTimeout(connectWebSocket, 1000);
  });

  ws.addEventListener("error", () => {
    els.connectionStatus.textContent = "Live error";
  });
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function bindEvents() {
  els.refreshRooms.addEventListener("click", () => refreshRooms().catch((error) => log("refresh failed", error.message)));
  els.createRoomForm.addEventListener("submit", (event) => createRoom(event).catch((error) => log("create failed", error.message)));
  els.joinRed.addEventListener("click", () => joinTeam("red").catch((error) => log("join red failed", error.message)));
  els.joinBlue.addEventListener("click", () => joinTeam("blue").catch((error) => log("join blue failed", error.message)));
  els.readyAll.addEventListener("click", () => readyAll().catch((error) => log("ready failed", error.message)));
  els.startRoom.addEventListener("click", () => startRoom().catch((error) => log("start failed", error.message)));
  els.closeRoom.addEventListener("click", () => closeRoom().catch((error) => log("close failed", error.message)));
  els.attackerSelect.addEventListener("change", () => {
    renderTargetOptions();
    renderBattle();
  });
  els.targetSelect.addEventListener("change", () => renderBattle());
  els.attackButton.addEventListener("click", () => attack().catch((error) => log("attack failed", error.message)));
  els.pauseBattle.addEventListener("click", () => pauseBattle().catch((error) => log("pause failed", error.message)));
  els.resumeBattle.addEventListener("click", () => resumeBattle().catch((error) => log("resume failed", error.message)));
  els.endBattle.addEventListener("click", () => endBattle().catch((error) => log("end battle failed", error.message)));
  els.clearLog.addEventListener("click", () => {
    els.eventLog.textContent = "";
  });
}

bindEvents();
render();
refreshRooms().catch((error) => log("initial refresh failed", error.message));
connectWebSocket();
