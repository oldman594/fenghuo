# Fenghuo

Fenghuo is a small AP-side battle runtime prototype. It currently supports room
creation, team join, ready/start flow, battle creation, hit simulation, battle
pause/resume/end, a web console, and a browser simulator.

## Prerequisites

- Linux or WSL
- Git
- Podman

On Ubuntu/WSL:

```bash
sudo apt-get update
sudo apt-get install -y git podman
```

## Clone

```bash
git clone <your-github-repo-url> fenghuo
cd fenghuo
```

Replace `<your-github-repo-url>` with the actual GitHub repository URL.

## Build The Development Image

The project uses a Podman development image with CMake, GCC, Boost, `just`, and
other build dependencies installed.

```bash
bash base/container/podman/cli/fenghuo.sh build
```

This creates the local image:

```text
localhost/fenghuo-dev:latest
```

## Start And Enter The Container

```bash
bash base/container/podman/cli/fenghuo.sh shell
```

After entering the container, the working directory is:

```text
/work/fenghuo
```

The repository is mounted into the container, so file changes are shared with the
host machine.

## Build The Project

Inside the container:

```bash
just build
```

This configures and builds the project into:

```text
build/
```

## Run Tests

Inside the container:

```bash
just test
```

You can also run tests from the host without manually entering the container:

```bash
bash base/container/podman/cli/fenghuo.sh run just test
```

## Start A Local Instance

Inside the container:

```bash
./build/fenghuo-apd --port 8080 --event-log-root /tmp/fenghuo-ap-events-dev
```

When startup succeeds, the terminal shows:

```text
fenghuo-apd listening on 0.0.0.0:8080
```

The container uses host networking, so open the web pages from the host browser.

## Web Testing

Open the console:

```text
http://127.0.0.1:8080/console
```

Basic console test flow:

1. Create a room.
2. Click `Join Red`.
3. Click `Join Blue`.
4. Click `Ready All`.
5. Click `Start`.
6. Use the battle panel to select an attacker and target.
7. Click `Attack`.
8. Use `Pause`, `Resume`, or `End` to control the battle.

Open the simulator:

```text
http://127.0.0.1:8080/sim
```

Basic simulator test flow:

1. Keep the default room ID or enter a new one.
2. Click `Setup 2v2`.
3. Wait for the status to become `Ready`.
4. Click `Run`.
5. Watch the simulated players move and attack.
6. Click `Pause` or `End` when needed.

## Common Commands

Build the container image:

```bash
bash base/container/podman/cli/fenghuo.sh build
```

Enter the container:

```bash
bash base/container/podman/cli/fenghuo.sh shell
```

Run one command in the container:

```bash
bash base/container/podman/cli/fenghuo.sh run <command>
```

Build and test in one command:

```bash
bash base/container/podman/cli/fenghuo.sh run just test
```

Clean build outputs inside the container:

```bash
just clean
```

## Troubleshooting

If port `8080` is already in use:

```bash
ss -ltnp | grep 8080
```

Stop the old process, or start Fenghuo on another port:

```bash
./build/fenghuo-apd --port 18080 --event-log-root /tmp/fenghuo-ap-events-dev
```

Then open:

```text
http://127.0.0.1:18080/console
```

If the browser seems to load old JavaScript, force refresh the page with
`Ctrl + F5`.
