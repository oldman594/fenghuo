const state = {
  rooms: [],
  roomDetails: new Map(),
  playerStatuses: new Map(),
  maps: new Map(),
  battles: new Map(),
  selectedRoomId: "",
  eventSequence: Date.now(),
  sourceId: sessionStorage.getItem("fenghuo_app_source_id") || `app-${Date.now()}-${Math.random().toString(16).slice(2)}`,
  autoBattle: {
    running: false,
    timerId: null,
    mode: "single_burst",
    lastError: "",
  },
};

sessionStorage.setItem("fenghuo_app_source_id", state.sourceId);

const els = {
  connectionStatus: document.querySelector("#connectionStatus"),
  refreshRooms: document.querySelector("#refreshRooms"),
  createRoomIdInput: document.querySelector("#createRoomIdInput"),
  createRoomNameInput: document.querySelector("#createRoomNameInput"),
  createRoomModeInput: document.querySelector("#createRoomModeInput"),
  createRoomMaxPlayersInput: document.querySelector("#createRoomMaxPlayersInput"),
  createRoom: document.querySelector("#createRoom"),
  roomList: document.querySelector("#roomList"),
  selectedRoomLabel: document.querySelector("#selectedRoomLabel"),
  roomSummary: document.querySelector("#roomSummary"),
  playerCount: document.querySelector("#playerCount"),
  playerStatusList: document.querySelector("#playerStatusList"),
  deviceCount: document.querySelector("#deviceCount"),
  deviceList: document.querySelector("#deviceList"),
  deviceIdInput: document.querySelector("#deviceIdInput"),
  deviceKindInput: document.querySelector("#deviceKindInput"),
  deviceNameInput: document.querySelector("#deviceNameInput"),
  bindPlayerSelect: document.querySelector("#bindPlayerSelect"),
  batteryInput: document.querySelector("#batteryInput"),
  signalInput: document.querySelector("#signalInput"),
  heartbeatOnlineSelect: document.querySelector("#heartbeatOnlineSelect"),
  registerDevice: document.querySelector("#registerDevice"),
  bindDevice: document.querySelector("#bindDevice"),
  heartbeatDevice: document.querySelector("#heartbeatDevice"),
  unbindDevice: document.querySelector("#unbindDevice"),
  mapPhase: document.querySelector("#mapPhase"),
  mapSnapshot: document.querySelector("#mapSnapshot"),
  positionPlayerSelect: document.querySelector("#positionPlayerSelect"),
  positionDeviceSelect: document.querySelector("#positionDeviceSelect"),
  positionXInput: document.querySelector("#positionXInput"),
  positionYInput: document.querySelector("#positionYInput"),
  positionHeadingInput: document.querySelector("#positionHeadingInput"),
  positionVelocityInput: document.querySelector("#positionVelocityInput"),
  submitPosition: document.querySelector("#submitPosition"),
  battlePhase: document.querySelector("#battlePhase"),
  battleSnapshot: document.querySelector("#battleSnapshot"),
  battleAttackerSelect: document.querySelector("#battleAttackerSelect"),
  battleTargetSelect: document.querySelector("#battleTargetSelect"),
  battleWeaponInput: document.querySelector("#battleWeaponInput"),
  battleHitZoneInput: document.querySelector("#battleHitZoneInput"),
  battleAmmoAfterInput: document.querySelector("#battleAmmoAfterInput"),
  battleDamageInput: document.querySelector("#battleDamageInput"),
  submitShot: document.querySelector("#submitShot"),
  submitHit: document.querySelector("#submitHit"),
  autoBattleModeSelect: document.querySelector("#autoBattleModeSelect"),
  autoBattleIntervalInput: document.querySelector("#autoBattleIntervalInput"),
  startAutoBattle: document.querySelector("#startAutoBattle"),
  stopAutoBattle: document.querySelector("#stopAutoBattle"),
  autoBattleStatus: document.querySelector("#autoBattleStatus"),
  pauseBattle: document.querySelector("#pauseBattle"),
  resumeBattle: document.querySelector("#resumeBattle"),
  endBattle: document.querySelector("#endBattle"),
  joinRed: document.querySelector("#joinRed"),
  joinBlue: document.querySelector("#joinBlue"),
  readyAll: document.querySelector("#readyAll"),
  startRoom: document.querySelector("#startRoom"),
  closeRoom: document.querySelector("#closeRoom"),
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

function selectedRoomSummary() {
  return state.rooms.find((room) => room.room_id === state.selectedRoomId) || null;
}

function selectedRoomDetail() {
  return state.roomDetails.get(state.selectedRoomId) || null;
}

function selectedMap() {
  return state.maps.get(state.selectedRoomId) || null;
}

function selectedBattle() {
  const room = selectedRoomSummary();
  return room?.battle_id ? state.battles.get(room.battle_id) || null : null;
}

function roomPlayers(detail) {
  return Object.values(detail?.players || {});
}

function roomPlayerStatuses(detail) {
  return roomPlayers(detail)
    .map((player) => state.playerStatuses.get(player.player_id))
    .filter(Boolean);
}

function roomDevices(detail) {
  return Object.values(detail?.devices || {});
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function formatMetric(value, digits = 1) {
  return Number.isFinite(value) ? Number(value).toFixed(digits) : "--";
}

function shortLabel(player) {
  const display = player?.display_name?.trim();
  if (display) {
    return display;
  }
  return player?.player_id || "unknown";
}

function teamClass(teamId) {
  return teamId === "red" ? "team-red" : "team-blue";
}

function aliveBattlePlayers() {
  const battle = selectedBattle();
  if (!battle?.players) {
    return [];
  }
  return Object.values(battle.players).filter((player) => player.alive !== false);
}

function renderRooms() {
  els.roomList.innerHTML = "";
  if (state.rooms.length === 0) {
    els.roomList.innerHTML = `<div class="muted">No rooms.</div>`;
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
      <span class="muted">${escapeHtml(room.room_id)} · ${room.player_count}/${room.max_players}</span>
    `;
    button.addEventListener("click", () => {
      state.selectedRoomId = room.room_id;
      refreshSelectedRoom().catch((error) => log("select room failed", error.message));
    });
    els.roomList.append(button);
  }
}

function renderRoomSummary() {
  const room = selectedRoomSummary();
  const detail = selectedRoomDetail();
  els.selectedRoomLabel.textContent = room?.room_id || "None";

  const canOperate = Boolean(room && room.phase === "open");
  els.joinRed.disabled = !canOperate;
  els.joinBlue.disabled = !canOperate;
  els.readyAll.disabled = !canOperate || roomPlayers(detail).length === 0;
  els.startRoom.disabled = !canOperate || roomPlayers(detail).length === 0;
  els.closeRoom.disabled = !(room && (room.phase === "open" || room.phase === "ended"));

  if (!room || !detail) {
    els.roomSummary.className = "roomDetail empty";
    els.roomSummary.textContent = "Select a room.";
    return;
  }

  const players = roomPlayers(detail);
  const rows = players.map((player) => `
    <tr>
      <td>${escapeHtml(player.display_name)}</td>
      <td class="team-${escapeHtml(player.team_id)}">${escapeHtml(player.team_id)}</td>
      <td>${player.ready ? "ready" : "not ready"}</td>
      <td>${escapeHtml(player.module_id || "")}</td>
    </tr>
  `).join("");

  els.roomSummary.className = "roomDetail";
  els.roomSummary.innerHTML = `
    <div class="summaryGrid">
      <div class="summaryCell"><span>Phase</span><strong>${escapeHtml(room.phase)}</strong></div>
      <div class="summaryCell"><span>Mode</span><strong>${escapeHtml(room.mode)}</strong></div>
      <div class="summaryCell"><span>Players</span><strong>${room.player_count}/${room.max_players}</strong></div>
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

function renderPlayerStatuses() {
  const detail = selectedRoomDetail();
  const statuses = roomPlayerStatuses(detail);
  els.playerCount.textContent = String(statuses.length);

  if (statuses.length === 0) {
    els.playerStatusList.className = "statusList empty";
    els.playerStatusList.textContent = "No player status.";
    return;
  }

  els.playerStatusList.className = "statusList";
  els.playerStatusList.innerHTML = statuses.map((status) => `
    <div class="statusRow">
      <div>
        <strong>${escapeHtml(status.display_name)}</strong>
        <div class="muted">${escapeHtml(status.player_id)} · ${escapeHtml(status.team_id)}</div>
      </div>
      <div class="statusMeta">
        <span>${status.ready ? "ready" : "not ready"}</span>
        <span>${status.alive === null ? "no battle" : status.alive ? "alive" : "down"}</span>
        <span>${status.health === null ? "-- hp" : `${status.health} hp`}</span>
      </div>
    </div>
  `).join("");
}

function renderDevices() {
  const detail = selectedRoomDetail();
  const devices = roomDevices(detail);
  els.deviceCount.textContent = String(devices.length);
  const players = roomPlayers(detail);

  const currentPlayer = els.bindPlayerSelect.value;
  els.bindPlayerSelect.innerHTML = "";
  for (const player of players) {
    els.bindPlayerSelect.add(new Option(`${player.display_name} (${player.player_id})`, player.player_id));
  }
  if (currentPlayer) {
    els.bindPlayerSelect.value = currentPlayer;
  }

  const room = selectedRoomSummary();
  const canOperate = Boolean(room && room.phase === "open");
  els.registerDevice.disabled = !canOperate;
  els.bindDevice.disabled = !canOperate || devices.length === 0 || players.length === 0;
  els.heartbeatDevice.disabled = !canOperate || devices.length === 0;
  els.unbindDevice.disabled = !canOperate || devices.length === 0;

  if (devices.length === 0) {
    els.deviceList.className = "statusList empty";
    els.deviceList.textContent = "No devices.";
    return;
  }

  els.deviceList.className = "statusList";
  els.deviceList.innerHTML = devices.map((device) => `
    <div class="statusRow">
      <div>
        <strong>${escapeHtml(device.display_name || device.device_id)}</strong>
        <div class="muted">${escapeHtml(device.device_id)} · ${escapeHtml(device.device_kind || "unknown")}</div>
      </div>
      <div class="statusMeta">
        <span>${device.online ? "online" : "offline"}</span>
        <span>${device.bound_player_id ? `bound ${escapeHtml(device.bound_player_id)}` : "unbound"}</span>
        <span>${device.battery_percent === null ? "-- batt" : `${device.battery_percent}% batt`}</span>
        <span>${device.signal_strength === null ? "-- signal" : `${device.signal_strength}% signal`}</span>
      </div>
    </div>
  `).join("");
}

function renderMap() {
  const detail = selectedRoomDetail();
  const map = selectedMap();
  const players = roomPlayers(detail);
  const devices = roomDevices(detail);
  const currentPlayer = els.positionPlayerSelect.value;
  const currentDevice = els.positionDeviceSelect.value;
  const boundDevices = currentPlayer
    ? devices.filter((device) => device.bound_player_id === currentPlayer)
    : devices;

  els.positionPlayerSelect.innerHTML = "";
  for (const player of players) {
    els.positionPlayerSelect.add(new Option(`${player.display_name} (${player.player_id})`, player.player_id));
  }
  if (currentPlayer && players.some((player) => player.player_id === currentPlayer)) {
    els.positionPlayerSelect.value = currentPlayer;
  } else if (players.length > 0) {
    els.positionPlayerSelect.value = players[0].player_id;
  }

  const selectedPlayerId = els.positionPlayerSelect.value;
  const filteredDevices = selectedPlayerId
    ? devices.filter((device) => device.bound_player_id === selectedPlayerId)
    : boundDevices;

  els.positionDeviceSelect.innerHTML = "";
  for (const device of filteredDevices) {
    const name = device.display_name || device.device_id;
    els.positionDeviceSelect.add(new Option(`${name} (${device.device_id})`, device.device_id));
  }
  if (currentDevice && filteredDevices.some((device) => device.device_id === currentDevice)) {
    els.positionDeviceSelect.value = currentDevice;
  } else if (filteredDevices.length > 0) {
    els.positionDeviceSelect.value = filteredDevices[0].device_id;
  }

  const room = selectedRoomSummary();
  const canReportPosition =
    Boolean(room && (room.phase === "open" || room.phase === "active")) &&
    players.length > 0 &&
    filteredDevices.length > 0;
  els.submitPosition.disabled = !canReportPosition;

  els.mapPhase.textContent = map?.phase || "None";
  if (!map || !map.positions || Object.keys(map.positions).length === 0) {
    els.mapSnapshot.className = "roomDetail empty";
    els.mapSnapshot.textContent = "No map snapshot.";
    return;
  }

  const world = Object.values(map.positions)
    .map((position) => {
      const player = detail?.players?.[position.player_id] || {};
      const status = state.playerStatuses.get(position.player_id) || null;
      const device = devices.find((item) => item.device_id === position.source_device_id) || null;
      return {
        ...position,
        player,
        status,
        device,
      };
    })
    .filter((item) => Number.isFinite(item.x) && Number.isFinite(item.y));

  if (world.length === 0) {
    els.mapSnapshot.className = "roomDetail empty";
    els.mapSnapshot.textContent = "No valid map coordinates.";
    return;
  }

  let minX = Math.min(...world.map((item) => item.x));
  let maxX = Math.max(...world.map((item) => item.x));
  let minY = Math.min(...world.map((item) => item.y));
  let maxY = Math.max(...world.map((item) => item.y));

  if (minX === maxX) {
    minX -= 10;
    maxX += 10;
  }
  if (minY === maxY) {
    minY -= 10;
    maxY += 10;
  }

  const paddingX = Math.max((maxX - minX) * 0.12, 4);
  const paddingY = Math.max((maxY - minY) * 0.12, 4);
  minX -= paddingX;
  maxX += paddingX;
  minY -= paddingY;
  maxY += paddingY;

  const width = maxX - minX;
  const height = maxY - minY;

  const markers = world.map((item) => {
    const left = clamp(((item.x - minX) / width) * 100, 4, 96);
    const top = clamp((1 - ((item.y - minY) / height)) * 100, 4, 96);
    const teamId = item.player.team_id || "blue";
    const online = item.status?.device_online ?? item.device?.online ?? false;
    const alive = item.status?.alive;
    const hp = item.status?.health;
    return `
      <div class="mapMarker" style="left:${left}%;top:${top}%;">
        <div class="mapMarkerCore ${teamClass(teamId)} ${alive === false ? "is-down" : ""} ${online ? "" : "is-offline"}">
          <div class="mapHeading" style="transform: translate(-50%, -90%) rotate(${Number(item.heading_deg) || 0}deg);"></div>
        </div>
        <div class="mapMarkerLabel">${escapeHtml(shortLabel(item.player))}${hp === null || hp === undefined ? "" : ` · ${hp}hp`}</div>
      </div>
    `;
  }).join("");

  const legend = world.map((item) => {
    const teamId = item.player.team_id || "blue";
    const online = item.status?.device_online ?? item.device?.online ?? false;
    const alive = item.status?.alive;
    const health = item.status?.health;
    return `
      <div class="mapLegendItem">
        <div class="mapLegendTop">
          <div class="mapLegendName">
            <span class="mapLegendSwatch ${teamClass(teamId)}"></span>
            <div>
              <strong>${escapeHtml(shortLabel(item.player))}</strong>
              <div class="muted">${escapeHtml(item.player_id)}</div>
            </div>
          </div>
          <div class="mapLegendMeta">
            <span class="pill ${online ? "online" : "offline"}">${online ? "online" : "offline"}</span>
            <span class="pill ${alive === false ? "down" : "alive"}">${alive === false ? "down" : "alive"}</span>
          </div>
        </div>
        <div class="mapLegendStats">
          <span>X ${formatMetric(item.x)}</span>
          <span>Y ${formatMetric(item.y)}</span>
          <span>Heading ${formatMetric(item.heading_deg, 0)}°</span>
          <span>Speed ${formatMetric(item.velocity_mps)} m/s</span>
          <span>HP ${health === null || health === undefined ? "--" : health}</span>
          <span>${escapeHtml(item.source_device_id || "no-device")}</span>
        </div>
      </div>
    `;
  }).join("");

  els.mapSnapshot.className = "roomDetail";
  els.mapSnapshot.innerHTML = `
    <div class="mapLayout">
      <div class="mapStage">
        <div class="mapBounds">
          <span>X ${formatMetric(minX)} .. ${formatMetric(maxX)}</span>
          <span>Y ${formatMetric(minY)} .. ${formatMetric(maxY)}</span>
        </div>
        ${markers}
      </div>
      <div class="mapLegend">
        <div class="mapLegendHeader">
          <span>Players</span>
          <span>${world.length}</span>
        </div>
        <div class="mapLegendList">
          ${legend}
        </div>
      </div>
    </div>
  `;
}

function renderBattle() {
  const battle = selectedBattle();
  syncBattleSelectors();
  els.battlePhase.textContent = battle?.phase || "None";
  els.pauseBattle.disabled = !(battle && battle.phase === "active");
  els.resumeBattle.disabled = !(battle && battle.phase === "paused");
  els.endBattle.disabled = !(battle && (battle.phase === "active" || battle.phase === "paused"));
  els.submitShot.disabled = !(battle && battle.phase === "active" && els.battleAttackerSelect.options.length > 0);
  els.submitHit.disabled = !(battle && battle.phase === "active" &&
    els.battleAttackerSelect.options.length > 0 && els.battleTargetSelect.options.length > 0);
  els.startAutoBattle.disabled = !(battle && battle.phase === "active" && !state.autoBattle.running &&
    els.battleAttackerSelect.options.length > 0 && els.battleTargetSelect.options.length > 0);
  els.stopAutoBattle.disabled = !state.autoBattle.running;
  els.autoBattleStatus.textContent = state.autoBattle.running
    ? `running · ${state.autoBattle.mode}${state.autoBattle.lastError ? ` · ${state.autoBattle.lastError}` : ""}`
    : state.autoBattle.lastError || "idle";
  els.autoBattleStatus.className = `inlineStatus ${state.autoBattle.running ? "running" : ""}`;
  if (!battle) {
    els.battleSnapshot.className = "roomDetail empty";
    els.battleSnapshot.textContent = "No battle snapshot.";
    return;
  }

  const players = Object.values(battle.players || {});
  const rows = players.map((player) => `
    <tr>
      <td>${escapeHtml(player.display_name)}</td>
      <td class="team-${escapeHtml(player.team_id)}">${escapeHtml(player.team_id)}</td>
      <td>${player.health}</td>
      <td>${player.alive ? "alive" : "down"}</td>
      <td>${player.shot_count}/${player.hit_count}</td>
    </tr>
  `).join("");

  els.battleSnapshot.className = "roomDetail";
  els.battleSnapshot.innerHTML = `
    <div class="summaryGrid">
      <div class="summaryCell"><span>Phase</span><strong>${escapeHtml(battle.phase)}</strong></div>
      <div class="summaryCell"><span>Mode</span><strong>${escapeHtml(battle.mode || "unknown")}</strong></div>
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

function render() {
  renderRooms();
  renderRoomSummary();
  renderPlayerStatuses();
  renderDevices();
  renderMap();
  renderBattle();
}

function upsertRoomSummary(room) {
  const index = state.rooms.findIndex((item) => item.room_id === room.room_id);
  if (index >= 0) {
    state.rooms[index] = room;
  } else {
    state.rooms.unshift(room);
  }
}

async function refreshRooms() {
  const body = await requestJson("/api/v1/rooms");
  state.rooms = body.rooms || [];
  if (state.selectedRoomId && !state.rooms.some((room) => room.room_id === state.selectedRoomId)) {
    state.selectedRoomId = "";
  }
  if (!state.selectedRoomId && state.rooms.length > 0) {
    state.selectedRoomId = state.rooms[0].room_id;
  }
  if (state.selectedRoomId) {
    await refreshSelectedRoom();
  } else {
    render();
  }
}

async function refreshSelectedRoom() {
  if (!state.selectedRoomId) {
    render();
    return;
  }

  const roomSummary = selectedRoomSummary();
  const [detail, map] = await Promise.all([
    requestJson(`/api/v1/rooms/${encodeURIComponent(state.selectedRoomId)}`),
    requestJson(`/api/v1/rooms/${encodeURIComponent(state.selectedRoomId)}/map`),
  ]);

  state.roomDetails.set(state.selectedRoomId, detail);
  state.maps.set(state.selectedRoomId, map);

  for (const player of roomPlayers(detail)) {
    try {
      const status = await requestJson(`/api/v1/players/${encodeURIComponent(player.player_id)}/status`);
      state.playerStatuses.set(player.player_id, status);
    } catch (error) {
      log("status refresh skipped", `${player.player_id}: ${error.message}`);
    }
  }

  if (roomSummary?.battle_id) {
    try {
      const battle = await requestJson(`/api/v1/battles/${encodeURIComponent(roomSummary.battle_id)}`);
      state.battles.set(roomSummary.battle_id, battle);
    } catch (error) {
      log("battle refresh skipped", `${roomSummary.battle_id}: ${error.message}`);
    }
  }

  render();
}

async function createRoom() {
  const roomId = els.createRoomIdInput.value.trim();
  const name = els.createRoomNameInput.value.trim();
  const mode = els.createRoomModeInput.value.trim() || "team_deathmatch";
  const maxPlayers = Number(els.createRoomMaxPlayersInput.value);
  const half = Math.max(1, Math.floor(maxPlayers / 2));

  const response = await requestJson("/api/v1/rooms", {
    method: "POST",
    body: JSON.stringify({
      ...nextEvent("app-room-create"),
      room_id: roomId || undefined,
      name: name || "App Room",
      mode,
      max_players: maxPlayers,
      teams: [
        { team_id: "red", display_name: "Red", max_players: half },
        { team_id: "blue", display_name: "Blue", max_players: Math.max(1, maxPlayers - half) },
      ],
    }),
  });
  log("create room", response);
  state.selectedRoomId = response.room.room_id;
  state.roomDetails.set(response.room.room_id, response);
  await refreshRooms();
}

async function joinTeam(teamId) {
  const room = selectedRoomSummary();
  const detail = selectedRoomDetail();
  if (!room || !detail) {
    return;
  }

  const existing = new Set(roomPlayers(detail).map((player) => player.player_id));
  let index = roomPlayers(detail).filter((player) => player.team_id === teamId).length + 1;
  while (existing.has(`p-${teamId}-${String(index).padStart(2, "0")}`)) {
    index += 1;
  }

  const response = await requestJson(`/api/v1/rooms/${encodeURIComponent(room.room_id)}/join`, {
    method: "POST",
    body: JSON.stringify({
      ...nextEvent(`app-${teamId}-join`),
      player_id: `p-${teamId}-${String(index).padStart(2, "0")}`,
      display_name: `${teamId === "red" ? "Red" : "Blue"} ${String(index).padStart(2, "0")}`,
      team_id: teamId,
      module_id: `module-${teamId}-${String(index).padStart(2, "0")}`,
    }),
  });
  log(`join ${teamId}`, response);
  state.roomDetails.set(room.room_id, response);
  await refreshRooms();
}

async function readyAll() {
  const room = selectedRoomSummary();
  const detail = selectedRoomDetail();
  if (!room || !detail) {
    return;
  }

  for (const player of roomPlayers(detail).filter((item) => !item.ready)) {
    const response = await requestJson(
      `/api/v1/rooms/${encodeURIComponent(room.room_id)}/players/${encodeURIComponent(player.player_id)}/ready`,
      {
        method: "POST",
        body: JSON.stringify({ ...nextEvent("app-ready"), ready: true }),
      },
    );
    state.roomDetails.set(room.room_id, response);
    log(`ready ${player.player_id}`, response);
  }
  await refreshSelectedRoom();
}

async function startRoom() {
  const room = selectedRoomSummary();
  if (!room) {
    return;
  }

  const response = await requestJson(`/api/v1/rooms/${encodeURIComponent(room.room_id)}/start`, {
    method: "POST",
    body: JSON.stringify({
      ...nextEvent("app-room-start"),
      battle_id: `${room.room_id}-battle`,
      duration_ms: 600000,
    }),
  });
  log("start room", response);
  state.roomDetails.set(room.room_id, response);
  if (response.battle_snapshot) {
    state.battles.set(response.battle_snapshot.battle_id, response.battle_snapshot);
  }
  await refreshRooms();
}

async function closeRoom() {
  const room = selectedRoomSummary();
  if (!room) {
    return;
  }

  const response = await requestJson(`/api/v1/rooms/${encodeURIComponent(room.room_id)}/close`, {
    method: "POST",
    body: JSON.stringify(nextEvent("app-room-close")),
  });
  log("close room", response);
  state.roomDetails.set(room.room_id, response);
  await refreshRooms();
}

async function registerDevice() {
  const room = selectedRoomSummary();
  if (!room) {
    return;
  }

  const response = await requestJson(`/api/v1/rooms/${encodeURIComponent(room.room_id)}/devices`, {
    method: "POST",
    body: JSON.stringify({
      ...nextEvent("app-device-register"),
      device_id: els.deviceIdInput.value.trim(),
      device_kind: els.deviceKindInput.value.trim(),
      display_name: els.deviceNameInput.value.trim(),
      battery_percent: Number(els.batteryInput.value),
      signal_strength: Number(els.signalInput.value),
    }),
  });
  log("register device", response);
  state.roomDetails.set(room.room_id, response);
  await refreshSelectedRoom();
}

async function bindDevice() {
  const room = selectedRoomSummary();
  const deviceId = els.deviceIdInput.value.trim();
  const playerId = els.bindPlayerSelect.value;
  if (!room || !deviceId || !playerId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/rooms/${encodeURIComponent(room.room_id)}/devices/${encodeURIComponent(deviceId)}/bind`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-device-bind"),
        player_id: playerId,
      }),
    },
  );
  log("bind device", response);
  state.roomDetails.set(room.room_id, response);
  await refreshSelectedRoom();
}

async function heartbeatDevice() {
  const room = selectedRoomSummary();
  const deviceId = els.deviceIdInput.value.trim();
  if (!room || !deviceId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/rooms/${encodeURIComponent(room.room_id)}/devices/${encodeURIComponent(deviceId)}/heartbeat`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-device-heartbeat"),
        battery_percent: Number(els.batteryInput.value),
        signal_strength: Number(els.signalInput.value),
        online: els.heartbeatOnlineSelect.value === "true",
      }),
    },
  );
  log("device heartbeat", response);
  state.roomDetails.set(room.room_id, response);
  await refreshSelectedRoom();
}

async function unbindDevice() {
  const room = selectedRoomSummary();
  const deviceId = els.deviceIdInput.value.trim();
  if (!room || !deviceId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/rooms/${encodeURIComponent(room.room_id)}/devices/${encodeURIComponent(deviceId)}/unbind`,
    {
      method: "POST",
      body: JSON.stringify(nextEvent("app-device-unbind")),
    },
  );
  log("unbind device", response);
  state.roomDetails.set(room.room_id, response);
  await refreshSelectedRoom();
}

async function reportPosition() {
  const room = selectedRoomSummary();
  const playerId = els.positionPlayerSelect.value;
  const deviceId = els.positionDeviceSelect.value;
  if (!room || !playerId || !deviceId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/rooms/${encodeURIComponent(room.room_id)}/positions`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-position"),
        player_id: playerId,
        source_device_id: deviceId,
        x: Number(els.positionXInput.value),
        y: Number(els.positionYInput.value),
        heading_deg: Number(els.positionHeadingInput.value),
        velocity_mps: Number(els.positionVelocityInput.value),
      }),
    },
  );
  log("report position", response);
  state.roomDetails.set(room.room_id, response);
  await refreshSelectedRoom();
}

async function battleCommand(command, body) {
  const battle = selectedBattle();
  if (!battle) {
    return;
  }

  const response = await requestJson(
    `/api/v1/battles/${encodeURIComponent(battle.battle_id)}/${command}`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent(`app-battle-${command}`),
        ...body,
      }),
    },
  );
  log(`battle ${command}`, response);
  state.battles.set(response.battle_id, response);
  render();
}

function battlePlayers() {
  const detail = selectedRoomDetail();
  const battle = selectedBattle();
  if (battle?.players) {
    return Object.values(battle.players);
  }
  return roomPlayers(detail);
}

function syncBattleSelectors() {
  const players = battlePlayers();
  const attackerValue = els.battleAttackerSelect.value;
  const targetValue = els.battleTargetSelect.value;

  els.battleAttackerSelect.innerHTML = "";
  els.battleTargetSelect.innerHTML = "";

  for (const player of players) {
    const label = `${player.display_name} (${player.player_id})`;
    els.battleAttackerSelect.add(new Option(label, player.player_id));
    els.battleTargetSelect.add(new Option(label, player.player_id));
  }

  if (attackerValue && players.some((player) => player.player_id === attackerValue)) {
    els.battleAttackerSelect.value = attackerValue;
  } else if (players.length > 0) {
    els.battleAttackerSelect.value = players[0].player_id;
  }

  if (targetValue && players.some((player) => player.player_id === targetValue)) {
    els.battleTargetSelect.value = targetValue;
  } else if (players.length > 1) {
    els.battleTargetSelect.value = players[1].player_id;
  } else if (players.length > 0) {
    els.battleTargetSelect.value = players[0].player_id;
  }
}

async function submitShot() {
  const battle = selectedBattle();
  const playerId = els.battleAttackerSelect.value;
  if (!battle || !playerId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/battles/${encodeURIComponent(battle.battle_id)}/shot`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-battle-shot"),
        player_id: playerId,
        weapon_id: els.battleWeaponInput.value.trim() || "rifle-01",
        ammo_after: Number(els.battleAmmoAfterInput.value),
      }),
    },
  );
  log("battle shot", response);
  state.battles.set(response.battle_id, response);
  render();
}

async function submitHit() {
  const battle = selectedBattle();
  const attackerPlayerId = els.battleAttackerSelect.value;
  const targetPlayerId = els.battleTargetSelect.value;
  if (!battle || !attackerPlayerId || !targetPlayerId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/battles/${encodeURIComponent(battle.battle_id)}/hit`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-battle-hit"),
        attacker_player_id: attackerPlayerId,
        target_player_id: targetPlayerId,
        weapon_id: els.battleWeaponInput.value.trim() || "rifle-01",
        damage: Number(els.battleDamageInput.value),
        hit_zone: els.battleHitZoneInput.value.trim() || "torso",
      }),
    },
  );
  log("battle hit", response);
  state.battles.set(response.battle_id, response);
  await refreshSelectedRoom();
}

function stopAutoBattle(reason = "") {
  if (state.autoBattle.timerId !== null) {
    clearTimeout(state.autoBattle.timerId);
  }
  state.autoBattle.running = false;
  state.autoBattle.timerId = null;
  state.autoBattle.lastError = reason;
  render();
}

async function sendShotForPlayer(playerId) {
  const battle = selectedBattle();
  if (!battle || !playerId) {
    return;
  }

  const currentPlayer = battle.players?.[playerId];
  const nextAmmo = Math.max(0, Number(currentPlayer?.ammo_remaining ?? els.battleAmmoAfterInput.value ?? 0) - 1);
  const response = await requestJson(
    `/api/v1/battles/${encodeURIComponent(battle.battle_id)}/shot`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-auto-shot"),
        player_id: playerId,
        weapon_id: els.battleWeaponInput.value.trim() || "rifle-01",
        ammo_after: nextAmmo,
      }),
    },
  );
  state.battles.set(response.battle_id, response);
}

async function sendHitBetween(attackerPlayerId, targetPlayerId) {
  const battle = selectedBattle();
  if (!battle || !attackerPlayerId || !targetPlayerId) {
    return;
  }

  const response = await requestJson(
    `/api/v1/battles/${encodeURIComponent(battle.battle_id)}/hit`,
    {
      method: "POST",
      body: JSON.stringify({
        ...nextEvent("app-auto-hit"),
        attacker_player_id: attackerPlayerId,
        target_player_id: targetPlayerId,
        weapon_id: els.battleWeaponInput.value.trim() || "rifle-01",
        damage: Number(els.battleDamageInput.value),
        hit_zone: els.battleHitZoneInput.value.trim() || "torso",
      }),
    },
  );
  state.battles.set(response.battle_id, response);
}

async function autoBattleStep() {
  const battle = selectedBattle();
  if (!state.autoBattle.running) {
    return;
  }
  if (!battle || battle.phase !== "active") {
    stopAutoBattle("battle stopped");
    return;
  }

  try {
    const mode = els.autoBattleModeSelect.value;
    state.autoBattle.mode = mode;
    state.autoBattle.lastError = "";

    if (mode === "single_burst") {
      const attackerPlayerId = els.battleAttackerSelect.value;
      const targetPlayerId = els.battleTargetSelect.value;
      const alive = aliveBattlePlayers().map((player) => player.player_id);
      if (!alive.includes(attackerPlayerId) || !alive.includes(targetPlayerId)) {
        stopAutoBattle("player down");
        return;
      }
      await sendShotForPlayer(attackerPlayerId);
      await sendHitBetween(attackerPlayerId, targetPlayerId);
    } else {
      const battlePlayersList = aliveBattlePlayers();
      const red = battlePlayersList.find((player) => player.team_id === "red");
      const blue = battlePlayersList.find((player) => player.team_id === "blue");
      if (!red || !blue) {
        stopAutoBattle("team eliminated");
        return;
      }

      const useRedAsAttacker = state.eventSequence % 2 === 0;
      const attackerPlayerId = useRedAsAttacker ? red.player_id : blue.player_id;
      const targetPlayerId = useRedAsAttacker ? blue.player_id : red.player_id;
      els.battleAttackerSelect.value = attackerPlayerId;
      els.battleTargetSelect.value = targetPlayerId;
      await sendShotForPlayer(attackerPlayerId);
      await sendHitBetween(attackerPlayerId, targetPlayerId);
    }

    await refreshSelectedRoom();
  } catch (error) {
    stopAutoBattle(error.message);
    log("auto battle failed", error.message);
    return;
  }

  const intervalMs = Math.max(100, Number(els.autoBattleIntervalInput.value) || 600);
  state.autoBattle.timerId = window.setTimeout(() => {
    autoBattleStep().catch((error) => {
      stopAutoBattle(error.message);
      log("auto battle failed", error.message);
    });
  }, intervalMs);
  render();
}

function startAutoBattle() {
  if (state.autoBattle.running) {
    return;
  }
  state.autoBattle.running = true;
  state.autoBattle.mode = els.autoBattleModeSelect.value;
  state.autoBattle.lastError = "";
  render();
  autoBattleStep().catch((error) => {
    stopAutoBattle(error.message);
    log("auto battle failed", error.message);
  });
}

function connectWebSocket() {
  const scheme = window.location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${scheme}://${window.location.host}/api/v0/live`);

  ws.addEventListener("open", () => {
    els.connectionStatus.textContent = "Live connected";
  });

  ws.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    if (message.type === "room_summary_updated" && message.room) {
      upsertRoomSummary(message.room);
    } else if (message.type === "room_detail_updated" && message.room?.room_id) {
      state.roomDetails.set(message.room.room_id, {
        room: message.room,
        players: message.players || {},
        devices: message.devices || {},
        positions: message.positions || {},
      });
    } else if (message.type === "player_status_updated") {
      if (message.status) {
        state.playerStatuses.set(message.player_id, message.status);
      } else {
        state.playerStatuses.delete(message.player_id);
      }
    } else if (message.type === "map_updated" && message.room_id) {
      state.maps.set(message.room_id, {
        room_id: message.room_id,
        phase: message.phase,
        positions: message.positions || {},
      });
    } else if (message.type === "accepted_event" && message.snapshot?.battle_id) {
      state.battles.set(message.snapshot.battle_id, message.snapshot);
    }
    log("websocket", message);
    render();
  });

  ws.addEventListener("close", () => {
    els.connectionStatus.textContent = "Live disconnected, retrying";
    setTimeout(connectWebSocket, 1000);
  });

  ws.addEventListener("error", () => {
    els.connectionStatus.textContent = "Live error";
  });
}

function bindEvents() {
  els.createRoom.addEventListener("click", () => createRoom().catch((error) => log("create room failed", error.message)));
  els.refreshRooms.addEventListener("click", () => refreshRooms().catch((error) => log("refresh failed", error.message)));
  els.joinRed.addEventListener("click", () => joinTeam("red").catch((error) => log("join red failed", error.message)));
  els.joinBlue.addEventListener("click", () => joinTeam("blue").catch((error) => log("join blue failed", error.message)));
  els.readyAll.addEventListener("click", () => readyAll().catch((error) => log("ready failed", error.message)));
  els.startRoom.addEventListener("click", () => startRoom().catch((error) => log("start failed", error.message)));
  els.closeRoom.addEventListener("click", () => closeRoom().catch((error) => log("close failed", error.message)));
  els.registerDevice.addEventListener("click", () => registerDevice().catch((error) => log("register device failed", error.message)));
  els.bindDevice.addEventListener("click", () => bindDevice().catch((error) => log("bind device failed", error.message)));
  els.heartbeatDevice.addEventListener("click", () => heartbeatDevice().catch((error) => log("heartbeat failed", error.message)));
  els.unbindDevice.addEventListener("click", () => unbindDevice().catch((error) => log("unbind device failed", error.message)));
  els.positionPlayerSelect.addEventListener("change", render);
  els.submitPosition.addEventListener("click", () => reportPosition().catch((error) => log("position failed", error.message)));
  els.submitShot.addEventListener("click", () => submitShot().catch((error) => log("shot failed", error.message)));
  els.submitHit.addEventListener("click", () => submitHit().catch((error) => log("hit failed", error.message)));
  els.startAutoBattle.addEventListener("click", () => startAutoBattle());
  els.stopAutoBattle.addEventListener("click", () => stopAutoBattle("stopped by operator"));
  els.pauseBattle.addEventListener("click", () => battleCommand("pause", { reason: "operator" }).catch((error) => log("pause failed", error.message)));
  els.resumeBattle.addEventListener("click", () => battleCommand("resume", {}).catch((error) => log("resume failed", error.message)));
  els.endBattle.addEventListener("click", () => battleCommand("end", { reason: "manual" }).catch((error) => log("end failed", error.message)));
  els.clearLog.addEventListener("click", () => {
    els.eventLog.textContent = "";
  });
}

bindEvents();
render();
refreshRooms().catch((error) => log("initial refresh failed", error.message));
connectWebSocket();
