#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLS_DIR="${ROOT_DIR}/tools/android"
LOCAL_DIR="${ROOT_DIR}/.local"
JDK_DIR="${LOCAL_DIR}/jdk"
FLUTTER_DIR="${LOCAL_DIR}/flutter"
ANDROID_DIR="${LOCAL_DIR}/android-sdk"
CMDLINE_DIR="${ANDROID_DIR}/cmdline-tools/latest"
JDK_ARCHIVE="${TOOLS_DIR}/OpenJDK17U-jdk_x64_linux.tar.gz"
FLUTTER_ARCHIVE="$(find "${TOOLS_DIR}" -maxdepth 1 -type f -name 'flutter_linux_*-stable.tar.xz' | head -n 1)"
CMDLINE_ARCHIVE="$(find "${TOOLS_DIR}" -maxdepth 1 -type f -name 'commandlinetools-linux-*_latest.zip' | head -n 1)"

mkdir -p "${LOCAL_DIR}" "${ANDROID_DIR}/cmdline-tools"

if [[ -f "${JDK_ARCHIVE}" && ! -d "${JDK_DIR}" ]]; then
  mkdir -p "${JDK_DIR}.tmp"
  tar -xzf "${JDK_ARCHIVE}" -C "${JDK_DIR}.tmp"
  mv "${JDK_DIR}.tmp"/* "${JDK_DIR}"
  rmdir "${JDK_DIR}.tmp"
fi

if [[ -n "${FLUTTER_ARCHIVE}" && ! -d "${FLUTTER_DIR}" ]]; then
  if tar -tJf "${FLUTTER_ARCHIVE}" >/dev/null 2>&1; then
    tar -xJf "${FLUTTER_ARCHIVE}" -C "${LOCAL_DIR}"
  else
    echo "warning: Flutter archive is incomplete or invalid: ${FLUTTER_ARCHIVE}" >&2
  fi
fi

if [[ -n "${CMDLINE_ARCHIVE}" && ! -d "${CMDLINE_DIR}" ]]; then
  mkdir -p "${CMDLINE_DIR}"
  export ROOT_DIR
  export CMDLINE_ARCHIVE
  python3 - <<'PY'
import os
import zipfile

archive = os.environ["CMDLINE_ARCHIVE"]
root = os.environ["ROOT_DIR"]
target = os.path.join(root, ".local", "android-sdk", "cmdline-tools", "latest")
with zipfile.ZipFile(archive) as zf:
    zf.extractall(target)
PY
  if [[ -d "${CMDLINE_DIR}/cmdline-tools" ]]; then
    mv "${CMDLINE_DIR}/cmdline-tools"/* "${CMDLINE_DIR}/"
    rmdir "${CMDLINE_DIR}/cmdline-tools"
  fi
fi

echo "Local SDK layout prepared under ${LOCAL_DIR}"
