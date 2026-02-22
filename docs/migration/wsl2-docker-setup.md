# WSL2 + Docker Desktop Startup Guide

This page is for Windows users developing in WSL2.

## 1. One-time prerequisites on Windows

1. Install Docker Desktop for Windows.
2. Make sure WSL2 is installed and your distro works (`wsl -l -v` in PowerShell).
3. Launch Docker Desktop at least once.

## 2. Enable WSL integration

In Docker Desktop:

1. Open `Settings`.
2. Open `Resources` -> `WSL Integration`.
3. Enable integration for your target distro (e.g. `Ubuntu`).
4. Click `Apply & Restart`.

## 3. Restart WSL after changing integration

Run in PowerShell:

```powershell
wsl --shutdown
```

Then reopen your WSL terminal.

## 4. Verify from WSL

Inside WSL:

```bash
docker version
docker info
```

Expected:

1. `docker` command exists.
2. Server section is visible in `docker version`.
3. `docker info` returns normally.

## 5. Run project dev shell

From repo root:

```bash
./scripts/dev-shell.sh
```

## 6. Common failure modes

### `docker: command not found`

Cause:

1. Docker Desktop WSL integration not enabled for this distro.
2. WSL terminal was not restarted after enabling.

Fix:

1. Enable integration in Docker Desktop.
2. `wsl --shutdown`
3. Reopen terminal.

### `Cannot connect to the Docker daemon`

Cause:

1. Docker Desktop is not running.
2. Integration got disabled after Docker/Desktop update.

Fix:

1. Start Docker Desktop.
2. Recheck integration toggle.
3. Retry `docker info`.

### Permission ownership mismatch on mounted workspace

Cause:

1. Container writes files as root.

Fix options:

1. Run `dev-shell.sh` with UID/GID mapping (recommended future improvement).
2. Correct ownership once:

```bash
sudo chown -R "$(id -u):$(id -g)" .
```

## 7. Notes for this project

1. Rootfs build uses `fakeroot`, so it does not require real root device creation on host.
2. Do not run `dockerd` manually inside WSL for this project.
   Use Docker Desktop engine via WSL integration.
3. If you are behind GFW (e.g. Clash on `7890`), also read:
   `docs/migration/network-proxy.md`
