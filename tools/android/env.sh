#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SDK_ROOT="${ROOT_DIR}/.local/android-sdk"
JAVA_ROOT="${ROOT_DIR}/.local/jdk"
FLUTTER_ROOT="${ROOT_DIR}/.local/flutter"

export ANDROID_SDK_ROOT="${SDK_ROOT}"
export ANDROID_HOME="${SDK_ROOT}"
export JAVA_HOME="${JAVA_ROOT}"
export FLUTTER_HOME="${FLUTTER_ROOT}"
export FLUTTER_STORAGE_BASE_URL="https://storage.flutter-io.cn"
export PUB_HOSTED_URL="https://pub.flutter-io.cn"

export PATH="${JAVA_HOME}/bin:${FLUTTER_HOME}/bin:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools:${PATH}"

echo "JAVA_HOME=${JAVA_HOME}"
echo "FLUTTER_HOME=${FLUTTER_HOME}"
echo "ANDROID_SDK_ROOT=${ANDROID_SDK_ROOT}"
echo "FLUTTER_STORAGE_BASE_URL=${FLUTTER_STORAGE_BASE_URL}"
echo "PUB_HOSTED_URL=${PUB_HOSTED_URL}"
