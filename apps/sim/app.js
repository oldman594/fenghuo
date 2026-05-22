const els = {
  status: document.querySelector("#status"),
  roomId: document.querySelector("#roomId"),
  durationSeconds: document.querySelector("#durationSeconds"),
  damage: document.querySelector("#damage"),
  setup: document.querySelector("#setup"),
  run: document.querySelector("#run"),
  pause: document.querySelector("#pause"),
  end: document.querySelector("#end"),
  log: document.querySelector("#log"),
  arena: document.querySelector("#arena"),
};

const ctx = els.arena.getContext("2d");
const sim = {
  room: null,
  battle: null,
  running: false,
  sequence: Date.now(),
  sourceId: sessionStorage.getItem("fenghuo_sim_source_id") || `sim-page-${Date.now()}-${Math.random().toString(16).slice(2)}`,
  players: [],
  startedAt: 0,
  durationMs: 60000,
  lastFrame: 0,
};

sessionStorage.setItem("fenghuo_sim_source_id", sim.sourceId);

const walls = [
  { x: 170, y: 90, w: 70, h: 250 },
  { x: 360, y: 0, w: 80, h: 220 },
  { x: 520, y: 340, w: 80, h: 220 },
  { x: 720, y: 160, w: 70, h: 260 },
  { x: 360, y: 270, w: 240, h: 36 },
];

const spawns = {
  red: [{ x: 80, y: 110 }, { x: 110, y: 450 }],
  blue: [{ x: 860, y: 110 }, { x: 830, y: 450 }],
};

function nowMs() {
  return Date.now();
}

function eventBase(prefix) {
  const sequence = sim.sequence++;
  return {
    event_id: `${prefix}-${nowMs()}-${sequence}`,
    source_id: sim.sourceId,
    sequence,
    occurred_at_ms: nowMs(),
  };
}

function log(label, value = "") {
  const text = typeof value === "string" ? value : JSON.stringify(value, null, 2);
  els.log.textContent = `[${new Date().toLocaleTimeString()}] ${label}\n${text}\n\n${els.log.textContent}`;
}

function setStatus(value) {
  els.status.textContent = value;
  renderControls();
}

function renderControls() {
  const hasActiveBattle = Boolean(sim.battle && sim.battle.phase === "active");
  els.run.disabled = !hasActiveBattle || sim.running;
  els.pause.disabled = !hasActiveBattle || !sim.running;
  els.end.disabled = !hasActiveBattle;
}

async function requestJson(path, options = {}) {
  const response = await fetch(path, {
    headers: { "content-type": "application/json" },
    ...options,
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(body.error?.message || response.statusText);
  }
  return body;
}

async function getRoom(roomId) {
  try {
    const response = await requestJson(`/api/v0/rooms/${encodeURIComponent(roomId)}`);
    return response.room;
  } catch {
    return null;
  }
}

async function getBattle(battleId) {
  try {
    const response = await requestJson(`/api/v0/battles/${encodeURIComponent(battleId)}/snapshot`);
    return response.snapshot;
  } catch {
    return null;
  }
}

async function setupRoom() {
  const roomId = els.roomId.value.trim() || "sim-room-001";
  const durationMs = Number(els.durationSeconds.value || 60) * 1000;
  sim.durationMs = durationMs;
  sim.running = false;
  setStatus("Setting up");
  sim.room = await getRoom(roomId);
  if (!sim.room) {
    const create = await requestJson("/api/v0/rooms", {
      method: "POST",
      body: JSON.stringify({
        ...eventBase("sim-room-created"),
        room_id: roomId,
        room_code: roomId,
        name: "Simulator room",
        mode: "team_deathmatch",
        max_players: 4,
        teams: [
          { team_id: "red", display_name: "Red", max_players: 2 },
          { team_id: "blue", display_name: "Blue", max_players: 2 },
        ],
      }),
    });
    sim.room = create.room;
  }

  if (sim.room.phase === "active" && sim.room.battle_id) {
    sim.battle = await getBattle(sim.room.battle_id);
    if (!sim.battle) {
      throw new Error("room is active but linked battle is not available");
    }
    initializeActors();
    sim.startedAt = nowMs();
    setStatus("Ready");
    log("loaded existing battle", { room: sim.room.room_id, battle: sim.battle.battle_id });
    draw();
    return;
  }

  if (sim.room.phase !== "open") {
    throw new Error(`room ${roomId} is ${sim.room.phase}; choose another Room ID`);
  }

  const roster = [
    ["red", "p-red-01", "Red 01"],
    ["red", "p-red-02", "Red 02"],
    ["blue", "p-blue-01", "Blue 01"],
    ["blue", "p-blue-02", "Blue 02"],
  ];
  for (const [team, playerId, displayName] of roster) {
    if (!sim.room.players[playerId]) {
      const joined = await requestJson(`/api/v0/rooms/${roomId}/players`, {
        method: "POST",
        body: JSON.stringify({
          ...eventBase("sim-player-joined"),
          player_id: playerId,
          display_name: displayName,
          team_id: team,
          module_id: `module-${playerId}`,
        }),
      });
      sim.room = joined.room;
    }
    if (!sim.room.players[playerId]?.ready) {
      const ready = await requestJson(`/api/v0/rooms/${roomId}/players/${playerId}/ready`, {
        method: "POST",
        body: JSON.stringify({ ...eventBase("sim-ready"), ready: true }),
      });
      sim.room = ready.room;
    }
  }

  const started = await requestJson(`/api/v0/rooms/${roomId}/start`, {
    method: "POST",
    body: JSON.stringify({
      ...eventBase("sim-room-started"),
      battle_id: `${roomId}-battle`,
      duration_ms: durationMs,
    }),
  });
  sim.room = started.room;
  sim.battle = started.battle_snapshot;
  initializeActors();
  sim.startedAt = nowMs();
  setStatus("Ready");
  log("setup complete", { room: sim.room.room_id, battle: sim.battle.battle_id });
  draw();
}

function initializeActors() {
  const teamIndexes = { red: 0, blue: 0 };
  sim.players = Object.values(sim.battle.players).map((player) => {
    const index = teamIndexes[player.team_id] || 0;
    teamIndexes[player.team_id] = index + 1;
    const spawn = spawns[player.team_id][index % spawns[player.team_id].length];
    return {
      id: player.player_id,
      team: player.team_id,
      x: spawn.x,
      y: spawn.y,
      vx: 0,
      vy: 0,
      cooldown: 0,
    };
  });
}

async function postBattleEvent(eventType, payload) {
  if (!sim.battle) {
    return null;
  }
  const response = await requestJson("/api/v0/events", {
    method: "POST",
    body: JSON.stringify({
      schema_version: 0,
      event_type: eventType,
      battle_id: sim.battle.battle_id,
      ...eventBase(`sim-${eventType}`),
      payload,
    }),
  });
  if (response.snapshot) {
    sim.battle = response.snapshot;
  }
  return response;
}

function run() {
  if (!sim.battle) {
    log("run blocked", "Setup a room first.");
    setStatus("Setup required");
    return;
  }
  if (sim.battle.phase !== "active") {
    log("run blocked", `Battle is ${sim.battle.phase}.`);
    setStatus("Not active");
    return;
  }
  sim.running = true;
  setStatus("Running");
}

function pause() {
  sim.running = false;
  setStatus("Paused");
}

async function end(reason = "manual") {
  if (!sim.battle || sim.battle.phase !== "active") {
    return;
  }
  await postBattleEvent("battle_ended", { reason });
  sim.running = false;
  setStatus("Ended");
  log("battle ended", reason);
}

function frame(timestamp) {
  const dt = Math.min(80, timestamp - (sim.lastFrame || timestamp));
  sim.lastFrame = timestamp;
  if (sim.running) {
    update(dt).catch((error) => log("update failed", error.message));
  }
  draw();
  requestAnimationFrame(frame);
}

async function update(dt) {
  if (!sim.battle || sim.battle.phase !== "active") {
    sim.running = false;
    renderControls();
    return;
  }
  if (nowMs() - sim.startedAt >= sim.durationMs) {
    await end("time_limit");
    return;
  }

  for (const actor of sim.players) {
    const snapshot = sim.battle.players[actor.id];
    if (!snapshot?.alive) {
      continue;
    }
    actor.cooldown = Math.max(0, actor.cooldown - dt);
    const target = nearestEnemy(actor);
    if (!target) {
      continue;
    }
    const dx = target.x - actor.x;
    const dy = target.y - actor.y;
    const dist = Math.hypot(dx, dy) || 1;
    if (dist > 190 || blocked(actor, target)) {
      moveToward(actor, target, dt);
    } else if (actor.cooldown <= 0) {
      actor.cooldown = 900;
      await attack(actor, target);
    }
  }
}

function nearestEnemy(actor) {
  return sim.players
    .filter((item) => item.team !== actor.team && sim.battle.players[item.id]?.alive)
    .sort((a, b) => distance(actor, a) - distance(actor, b))[0] || null;
}

function moveToward(actor, target, dt) {
  const dx = target.x - actor.x;
  const dy = target.y - actor.y;
  const dist = Math.hypot(dx, dy) || 1;
  const speed = 0.08 * dt;
  const next = {
    x: actor.x + (dx / dist) * speed,
    y: actor.y + (dy / dist) * speed,
  };
  if (!insideWall(next.x, next.y)) {
    actor.x = clamp(next.x, 24, 936);
    actor.y = clamp(next.y, 24, 536);
  } else {
    actor.x = clamp(actor.x + (dy / dist) * speed, 24, 936);
    actor.y = clamp(actor.y - (dx / dist) * speed, 24, 536);
  }
}

async function attack(actor, target) {
  const attacker = sim.battle.players[actor.id];
  const damage = Number(els.damage.value || 10);
  await postBattleEvent("shot", {
    player_id: actor.id,
    weapon_id: "sim-rifle",
    ammo_after: Math.max(0, Number(attacker.ammo || 30) - 1),
  });
  const hit = await postBattleEvent("hit", {
    attacker_player_id: actor.id,
    target_player_id: target.id,
    weapon_id: "sim-rifle",
    damage,
    hit_zone: "torso",
  });
  log("hit", {
    attacker: actor.id,
    target: target.id,
    target_health: hit.snapshot.players[target.id].health,
  });
  maybeEndForElimination().catch((error) => log("end check failed", error.message));
}

async function maybeEndForElimination() {
  const liveTeams = new Set(
    Object.values(sim.battle.players)
      .filter((player) => player.alive)
      .map((player) => player.team_id),
  );
  if (liveTeams.size <= 1 && sim.battle.phase === "active") {
    await end("elimination");
  }
}

function draw() {
  ctx.clearRect(0, 0, 960, 560);
  ctx.fillStyle = "#c6b58b";
  ctx.fillRect(0, 0, 960, 560);
  ctx.fillStyle = "#b49f73";
  ctx.fillRect(30, 40, 180, 110);
  ctx.fillRect(750, 390, 180, 120);
  ctx.fillStyle = "#786447";
  for (const wall of walls) {
    ctx.fillRect(wall.x, wall.y, wall.w, wall.h);
  }
  for (const player of sim.players) {
    const snapshot = sim.battle?.players[player.id];
    ctx.beginPath();
    ctx.fillStyle = player.team === "red" ? "#dc2626" : "#2563eb";
    ctx.globalAlpha = snapshot?.alive === false ? 0.35 : 1;
    ctx.arc(player.x, player.y, 12, 0, Math.PI * 2);
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.fillStyle = "#111827";
    ctx.font = "12px sans-serif";
    ctx.fillText(`${player.id} ${snapshot?.health ?? 100}`, player.x + 14, player.y + 4);
  }
}

function distance(a, b) {
  return Math.hypot(a.x - b.x, a.y - b.y);
}

function blocked(a, b) {
  for (const wall of walls) {
    for (let step = 0; step <= 10; ++step) {
      const t = step / 10;
      const x = a.x + (b.x - a.x) * t;
      const y = a.y + (b.y - a.y) * t;
      if (x >= wall.x && x <= wall.x + wall.w && y >= wall.y && y <= wall.y + wall.h) {
        return true;
      }
    }
  }
  return false;
}

function insideWall(x, y) {
  return walls.some((wall) => x >= wall.x - 12 && x <= wall.x + wall.w + 12 &&
    y >= wall.y - 12 && y <= wall.y + wall.h + 12);
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

els.setup.addEventListener("click", () => setupRoom().catch((error) => log("setup failed", error.message)));
els.run.addEventListener("click", run);
els.pause.addEventListener("click", pause);
els.end.addEventListener("click", () => end("manual").catch((error) => log("end failed", error.message)));

requestAnimationFrame(frame);
renderControls();
draw();
