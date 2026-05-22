#!/usr/bin/env bash
set -euo pipefail

readonly FENGHUO_NAME="fenghuo-dev"
readonly FENGHUO_IMAGE="localhost/fenghuo-dev:latest"
readonly FENGHUO_REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"
readonly FENGHUO_IMAGE_DIR="${FENGHUO_REPO_ROOT}/base/container/podman/image"
readonly FENGHUO_STATE_DIR="${FENGHUO_DATA_ROOT:-${HOME}/.local/share/fenghuo}"

usage() {
  cat <<'EOF'
usage: base/container/podman/cli/fenghuo.sh {build|run|shell}
       base/container/podman/cli/fenghuo.sh run [COMMAND...]
EOF
}

ensure_prereqs() {
  if ! command -v podman >/dev/null; then
    cat >&2 <<'EOF'
error: podman is required but was not found in PATH.

Install Podman, then rerun this command. On Ubuntu/WSL:
  sudo apt-get update
  sudo apt-get install -y podman
EOF
    exit 127
  fi
}

ensure_dir() {
  mkdir -p "$1"
}

ensure_file() {
  mkdir -p "$(dirname "$1")"
  touch "$1"
}

init_host_mounts() {
  ensure_dir "${FENGHUO_STATE_DIR}/cache"
  ensure_dir "${FENGHUO_STATE_DIR}/ccache"
  ensure_dir "${FENGHUO_STATE_DIR}/build"
  ensure_dir "${FENGHUO_STATE_DIR}/deps"
  ensure_dir "${FENGHUO_STATE_DIR}/home-cache"
  ensure_dir "${HOME}/.config"
  ensure_dir "${HOME}/.local"
  ensure_dir "${HOME}/.ssh"
  ensure_dir "${HOME}/.gnupg"
  ensure_file "${HOME}/.gitconfig"
  ensure_file "${HOME}/.git-credentials"
  ensure_file "${HOME}/.netrc"
}

build_image() {
  podman build \
    -t "${FENGHUO_IMAGE}" \
    -f "${FENGHUO_IMAGE_DIR}/Containerfile" \
    "${FENGHUO_REPO_ROOT}"
}

run_container() {
  local run_mode="$1"
  shift || true

  init_host_mounts

  local -a run_args=(
    --user root
    --userns=keep-id
    --group-add keep-groups
    --network host
    --env "FENGHUO_USER_NAME=$(whoami)"
    --env "FENGHUO_USER_UID=$(id --user)"
    --env "FENGHUO_USER_GID=$(id --group)"
    --env "FENGHUO_USER_GROUP=$(id --group --name)"
    --workdir /work/fenghuo
    --volume "${FENGHUO_REPO_ROOT}:/work/fenghuo:rw,rslave"
    --volume "${FENGHUO_STATE_DIR}/cache:/work/.cache:rw,rslave"
    --volume "${FENGHUO_STATE_DIR}/ccache:/work/.ccache:rw,rslave"
    --volume "${FENGHUO_STATE_DIR}/build:/work/build:rw,rslave"
    --volume "${FENGHUO_STATE_DIR}/deps:/work/deps:rw,rslave"
    --volume "${FENGHUO_STATE_DIR}/home-cache:/home/$(whoami)/.cache/fenghuo:rw,rslave"
    --volume "${HOME}/.config:/home/$(whoami)/.config:rw,rslave"
    --volume "${HOME}/.local:/home/$(whoami)/.local:rw,rslave"
    --volume "${HOME}/.ssh:/home/$(whoami)/.ssh:rw,rslave"
    --volume "${HOME}/.gitconfig:/home/$(whoami)/.gitconfig:rw,rslave"
    --volume "${HOME}/.git-credentials:/home/$(whoami)/.git-credentials:rw,rslave"
    --volume "${HOME}/.gnupg:/home/$(whoami)/.gnupg:rw,rslave"
    --volume "${HOME}/.netrc:/home/$(whoami)/.netrc:rw,rslave"
  )

  if [[ "${run_mode}" == "shell" ]]; then
    run_args+=(
      --replace
      --name "${FENGHUO_NAME}-shell"
      --rm
      -it
    )
    podman run "${run_args[@]}" "${FENGHUO_IMAGE}" /bin/bash
    return
  fi

  run_args+=(--rm)
  podman run "${run_args[@]}" "${FENGHUO_IMAGE}" "$@"
}

command="${1:-shell}"

case "${command}" in
  build)
    ensure_prereqs
    build_image
    ;;
  run)
    shift
    if [ "$#" -eq 0 ]; then
      set -- just test
    fi
    ensure_prereqs
    run_container run "$@"
    ;;
  shell)
    ensure_prereqs
    run_container shell
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
