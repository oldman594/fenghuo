# RFD: Android App V1 architecture and setup

- Status: Draft
- Owners: apps
- Created: 2026-06-01
- Updated: 2026-06-01

## Summary

This step starts the first real Android app for `fenghuo` with the narrow goal of integration and
closed-loop validation.

The app is not a production player app yet. It is an Android-first field debug client that consumes
the existing `api/v1` and live WebSocket surfaces, runs on real Android phones, and can be built as
an APK from WSL Ubuntu.

## Decision

### Selected stack: Flutter

Choose Flutter for App V1.

Reasons:

- fastest path to an installable Android app while backend contracts are still evolving
- one UI codebase can later cover Android, iOS, and desktop debug needs
- strong support for HTTP, WebSocket, reactive state, and fast iteration
- command-line build flow works in WSL without requiring Android Studio as the primary workflow

Do not choose Kotlin native for this phase because:

- the immediate goal is integration speed, not Android-specific polish
- product surface is still changing
- we benefit from a shared cross-platform UI/runtime later

Kotlin remains acceptable for a future production rewrite if Android-only ownership becomes a hard
constraint.

## Scope

This step includes:

- Flutter Android app skeleton
- Android debug build from WSL
- pages required for backend integration and field validation
- environment scripts for local SDK setup in user space

This step does not include:

- app store release setup
- authentication
- operator role and player role separation
- production branding and final visual design
- offline sync

## Page structure

App V1 pages:

1. room list
2. room detail
3. map view
4. player status panel
5. battle status panel
6. debug actions panel
7. settings / backend endpoint configuration

Recommended navigation:

- bottom navigation with:
  - Rooms
  - Battle
  - Map
  - Settings

Room detail may present player/device/debug actions in stacked sections inside the selected room
flow rather than as a separate route.

## API consumption scope

### HTTP queries

```text
GET /api/v1/rooms
GET /api/v1/rooms/{room_id}
GET /api/v1/rooms/{room_id}/map
GET /api/v1/players/{player_id}/status
GET /api/v1/battles/{battle_id}
```

### HTTP commands

```text
POST /api/v1/rooms
POST /api/v1/rooms/{room_id}/join
POST /api/v1/rooms/{room_id}/leave
POST /api/v1/rooms/{room_id}/players/{player_id}/ready
POST /api/v1/rooms/{room_id}/start
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

### WebSocket

```text
WS /api/v0/live
```

Consume at least:

- `room_summary_updated`
- `room_detail_updated`
- `player_status_updated`
- `map_updated`
- `accepted_event`

## Android integration workflow

### Development host

- WSL Ubuntu for source editing, Flutter commands, and APK build
- Android phone for install and validation
- Windows host network path used by the phone to reach the backend running in WSL

### Backend addressing

The app must not assume `127.0.0.1`.

Use a configurable base URL such as:

```text
http://192.168.x.x:8080
```

where `192.168.x.x` is the Windows host LAN address reachable by the Android phone.

### Build workflow

- install Flutter SDK in user space
- install JDK 17 in user space
- install Android command-line tools and required SDK packages in user space
- export environment through a repo-owned shell script
- build APK with Flutter CLI from WSL

### Real-device testing

The maintainer performs:

- USB or local-network device validation
- APK install on the Android phone
- final endpoint and LAN verification

## Project layout

Add a new Flutter app under:

```text
apps/android_client
```

Add local helper scripts under:

```text
tools/android/
```

## Validation

- `flutter doctor`
- `flutter analyze`
- `flutter test`
- `flutter build apk --debug`
- existing backend `just test`
