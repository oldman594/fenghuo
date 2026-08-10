# RFD: Player App V1 information architecture

- Status: Draft
- Owners: apps
- Created: 2026-06-03
- Updated: 2026-06-03

## Summary

This step defines the first real player-facing mobile app for `fenghuo`.

The current Flutter Android client is a field debug client. It is useful for integration, but it is
not yet a product-shaped player app. Player App V1 should now become a separate target with a clear
information architecture, player-focused page structure, a restrained visual direction, and a
limited backend contract.

This app is still a V1. It is intended for closed-loop field validation and pilot use, not for app
store release or full production rollout.

## Product role

Player App V1 serves the player during a live match.

It should let a player:

- discover joinable rooms
- enter a room lobby
- view team and ready state
- enter a live battle view
- see own state, team state, map state, and battle timer
- view a simple result screen when the battle ends

It should not act as an operator console, referee console, or backend debug shell.

## Scope

This step includes:

- player-oriented information architecture
- player-oriented mobile page structure
- minimal player-facing visual direction
- API surface needed by the player app
- explicit role boundary against `/app` and `/console`

This step does not include:

- operator workflows
- room creation and device binding as primary player flows
- battle debug commands
- hardware diagnostics UI
- production account system
- payments, friends, chat, or social systems

## Primary app states

Player App V1 should be designed around five user states:

1. not in room
2. in room lobby
3. waiting for battle start
4. battle active
5. battle ended

The app should change emphasis based on state instead of exposing every function at once.

## Information architecture

Recommended top-level information architecture:

1. Home
2. Room
3. Battle
4. Me

### Home

Purpose:

- backend connection summary
- discover open rooms
- select and enter a room

Core information:

- room name
- room mode
- room phase
- current player count / max players
- whether a room is joinable

### Room

Purpose:

- show the selected room lobby
- show team layout and player readiness
- let the player join or leave
- let the player toggle ready when that API exists for the player role

Core information:

- room title and mode
- room phase
- team blocks
- players in each team
- ready / not ready state
- battle start hint when room becomes active

### Battle

Purpose:

- become the primary in-match screen
- show live map and live battle state
- show player survival state, score, timer, and team status

Core information:

- 2D map
- self marker
- teammate markers
- optional enemy markers if rules allow
- battle timer
- team score
- own HP / alive state
- match status: active / paused / ended

### Me

Purpose:

- show personal player state and local app settings
- show device and connection summaries without exposing operator actions

Core information:

- player id / display name
- team
- alive state / HP
- bound device summary
- backend base URL
- WebSocket connection state

## Page structure

Recommended navigation:

- bottom navigation with:
  - Home
  - Room
  - Battle
  - Me

The `Room` and `Battle` tabs may be disabled or show empty-state guidance when the player is not in
that phase yet.

## Page sketches

These are structural sketches, not pixel-perfect layouts.

### Home

```text
+--------------------------------------------------+
| Fenghuo                               online     |
| backend: 192.168.x.x:8080                        |
+--------------------------------------------------+
| Rooms                                            |
| [Search / refresh]                               |
|                                                  |
| Room A        open            3 / 8        >     |
| TDM           joinable                             |
|                                                  |
| Room B        active          8 / 8        >     |
| TDM           in match                              |
|                                                  |
| Room C        closed          0 / 8        >     |
+--------------------------------------------------+
```

### Room

```text
+--------------------------------------------------+
| Room A                             open          |
| team_deathmatch                                   |
+--------------------------------------------------+
| Red Team                      Blue Team          |
| p-red-01   ready              p-blue-01 ready    |
| p-red-02   not ready          p-blue-02 ready    |
|                                                  |
+--------------------------------------------------+
| [Join Red] [Join Blue] [Leave Room]              |
| [Ready]                                          |
+--------------------------------------------------+
```

### Battle

```text
+--------------------------------------------------+
| Room A                      08:32       active   |
| Red 3                                  Blue 1    |
+--------------------------------------------------+
|                                                  |
|                2D MAP CANVAS                     |
|                                                  |
|      B1                        R2                |
|                                                  |
|                YOU (R1)                          |
|                                                  |
|                              B2                  |
|                                                  |
+--------------------------------------------------+
| HP 70      Team Red      alive      ws online    |
+--------------------------------------------------+
```

### Me

```text
+--------------------------------------------------+
| Me                                               |
+--------------------------------------------------+
| Player: p-red-01                                 |
| Team: red                                        |
| HP: 70                                           |
| State: alive                                     |
| Device: headset-red-01                           |
| Backend: http://192.168.x.x:8080                 |
| Live: connected                                  |
+--------------------------------------------------+
```

### Result

This may be a routed page or a modal-style full-screen sheet after battle end.

```text
+--------------------------------------------------+
| Battle Ended                                     |
| Red Team Wins                                    |
+--------------------------------------------------+
| Your status: alive                               |
| Team score: 10                                   |
| Enemy score: 6                                   |
| Hits: 4                                          |
| Deaths: 1                                        |
+--------------------------------------------------+
| [Back to Room]   [Back to Home]                  |
+--------------------------------------------------+
```

## Visual direction

Player App V1 should not look like the current debug page.

It should borrow from tactical map products and location-based multiplayer game UIs:

- map-first during battle
- dense but calm information hierarchy
- restrained, work-focused surfaces
- strong team color cues
- minimal decorative chrome

### Visual principles

- use a dark neutral base in battle views
- reserve red and blue as semantic team colors, not background themes
- keep cards and panels compact
- prefer full-width sections over nested card stacks
- keep the battle map dominant in the viewport
- use bottom sheets or segmented sections for secondary details
- avoid marketing-style hero layouts, oversized banners, and decorative gradients

### Recommended component tone

- map canvas as the main surface
- compact status bars for score, timer, connection, and HP
- list rows for rooms and players
- icon-supported controls for refresh, reconnect, and navigation
- buttons only for clear player actions such as join, leave, and ready

### Recommended color usage

- neutral dark gray / graphite base
- red team accent
- blue team accent
- green only for successful connection / ready / healthy states
- amber for warning states such as reconnecting or waiting

## API consumption scope

Player App V1 should consume a narrower contract than `/app`.

### Required queries

```text
GET /api/v1/rooms
GET /api/v1/rooms/{room_id}
GET /api/v1/rooms/{room_id}/map
GET /api/v1/players/{player_id}/status
GET /api/v1/battles/{battle_id}
```

### Required WebSocket

```text
WS /api/v0/live
```

Consume at least:

- `room_summary_updated`
- `room_detail_updated`
- `player_status_updated`
- `map_updated`

The app may also consume:

- `accepted_event`

but should treat it as a secondary log/debug stream, not as its primary presentation model.

### Player-facing write commands

Player App V1 should primarily use:

```text
POST /api/v1/rooms/{room_id}/join
POST /api/v1/rooms/{room_id}/leave
POST /api/v1/rooms/{room_id}/players/{player_id}/ready
```

Optional for early pilot use:

```text
POST /api/v1/rooms
```

but this should not remain a primary player flow in the long term.

### Explicitly excluded from Player App V1 primary UI

Do not expose these as normal player controls:

```text
POST /api/v1/rooms/{room_id}/close
POST /api/v1/rooms/{room_id}/devices
POST /api/v1/rooms/{room_id}/devices/{device_id}/bind
POST /api/v1/rooms/{room_id}/devices/{device_id}/unbind
POST /api/v1/rooms/{room_id}/devices/{device_id}/heartbeat
POST /api/v1/rooms/{room_id}/positions
POST /api/v1/battles/{battle_id}/pause
POST /api/v1/battles/{battle_id}/resume
POST /api/v1/battles/{battle_id}/end
POST /api/v1/battles/{battle_id}/shot
POST /api/v1/battles/{battle_id}/hit
```

Those remain operator, simulator, or debug responsibilities.

## Responsibility boundaries

The project now has three distinct frontends with different roles.

### Player App V1

Audience:

- players

Purpose:

- discover rooms
- join a room
- ready up
- watch live battle state
- view own status and outcome

Must not become:

- a device lab
- a battle command console
- a referee tool

### `/app`

Audience:

- developers
- integrators
- internal testers

Purpose:

- broad API validation
- synthetic flows
- device registration and binding debug
- position injection
- shot / hit debug
- quick end-to-end inspection without installing mobile builds

`/app` is the backend-facing debug surface and should stay permissive.

### `/console`

Audience:

- operators
- staff
- referees

Purpose:

- room orchestration
- team setup
- room start / close
- battle supervision
- operator control actions

`/console` should remain the place for staff control even after the player app matures.

## Recommended implementation order

1. refine Flutter navigation from debug tabs into player-oriented tabs
2. redesign Home and Room pages around player flows
3. promote Battle page into the main in-match surface
4. simplify Me page and remove operator/debug emphasis
5. keep `/app` as the full debug surface without trying to mirror it in mobile

## Validation

Player App V1 should be considered structurally ready when:

- a player can open the app and discover rooms
- a player can join and leave a room
- a player can see team membership and ready state
- a player can enter a battle view with a live map and battle status
- a player can see battle end state and return to lobby/home
- no operator-only control is required for normal player usage
