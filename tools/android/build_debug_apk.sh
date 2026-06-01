#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "${ROOT_DIR}/tools/android/env.sh"

cd "${ROOT_DIR}/apps/android_client"
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
