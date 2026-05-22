#!/usr/bin/env bash
set -euo pipefail

runtime_user="${FENGHUO_USER_NAME:-developer}"
runtime_uid="${FENGHUO_USER_UID:-1000}"
runtime_gid="${FENGHUO_USER_GID:-1000}"
runtime_group="${FENGHUO_USER_GROUP:-${runtime_user}}"
runtime_home="/home/${runtime_user}"

if [ "$(id -u)" = "${runtime_uid}" ] && [ "$(id -g)" = "${runtime_gid}" ]; then
  export HOME="${runtime_home}"
  export XDG_CACHE_HOME="/work/.cache"
  export CCACHE_DIR="/work/.ccache"
  export FENGHUO_DEPS_DIR="/work/deps"
  export FENGHUO_BUILD_ROOT="/work/build"
  if [ "$#" -eq 0 ]; then
    set -- /bin/bash
  fi
  exec "$@"
fi

if ! getent group "${runtime_gid}" >/dev/null; then
  groupadd --gid "${runtime_gid}" "${runtime_group}"
fi

existing_user="$(getent passwd "${runtime_uid}" | cut -d: -f1 || true)"
if [ -z "${existing_user}" ]; then
  mkdir -p "${runtime_home}"
  useradd \
    --uid "${runtime_uid}" \
    --gid "${runtime_gid}" \
    --no-create-home \
    --home-dir "${runtime_home}" \
    --shell /bin/bash \
    "${runtime_user}"
else
  runtime_user="${existing_user}"
  runtime_home="$(getent passwd "${runtime_user}" | cut -d: -f6)"
fi

mkdir -p \
  "${runtime_home}" \
  /work/fenghuo \
  /work/.cache \
  /work/.ccache \
  /work/build \
  /work/deps

for owned_path in "${runtime_home}" /work/.cache /work/.ccache /work/build /work/deps; do
  chown -R "${runtime_uid}:${runtime_gid}" "${owned_path}" 2>/dev/null || true
done

export HOME="${runtime_home}"
export XDG_CACHE_HOME="/work/.cache"
export CCACHE_DIR="/work/.ccache"
export FENGHUO_DEPS_DIR="/work/deps"
export FENGHUO_BUILD_ROOT="/work/build"

if [ "$#" -eq 0 ]; then
  set -- /bin/bash
fi

exec setpriv \
  --reuid="${runtime_uid}" \
  --regid="${runtime_gid}" \
  --clear-groups \
  "$@"
