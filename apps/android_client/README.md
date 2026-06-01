# Fenghuo Android Client

This is the Flutter-based Android integration client for `fenghuo`.

## Current status

This is an App V1 skeleton for integration testing. It currently provides:

- backend base URL configuration
- room list loading
- selected room detail loading
- map snapshot loading
- battle snapshot loading
- WebSocket live log view
- room creation from mobile client

## Current app structure

```text
lib/
  main.dart
  src/
    app.dart
    models/
    services/
    state/
    pages/
    widgets/
```

The current client consumes:

- `GET /api/v1/rooms`
- `GET /api/v1/rooms/{room_id}`
- `GET /api/v1/rooms/{room_id}/map`
- `GET /api/v1/players/{player_id}/status`
- `GET /api/v1/battles/{battle_id}`
- `POST /api/v1/rooms`
- `WS /api/v0/live`

## Local SDK setup in WSL

From repo root:

```bash
cd ~/fenghuo
bash tools/android/setup_local_sdk.sh
source tools/android/env.sh
```

## Expected toolchain

- JDK 17
- Flutter 3.44.0 stable
- Android SDK command-line tools
- Android platform-tools
- Android platform 35
- Android build-tools 35.0.0

## Build commands

From repo root:

```bash
source tools/android/env.sh
cd apps/android_client
flutter doctor
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
```

## Backend address

Do not use `127.0.0.1` on the phone.

Use the Windows host LAN address, for example:

```text
http://192.168.1.100:8080
```
