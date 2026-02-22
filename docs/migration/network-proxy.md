# Network/Proxy Guide (WSL2 + Docker, GFW)

This project supports two proxy modes for developers behind GFW.

## Mode A: Clash TUN (preferred if stable)

When Clash TUN mode is enabled and your WSL2 traffic is already routed, you can usually run directly:

```bash
./scripts/dev-shell.sh
```

No explicit proxy env is needed.

## Mode B: Explicit HTTP(S) proxy (Clash 7890)

Use this when TUN mode is unavailable/unstable, or docker build cannot reach upstream mirrors.

Important:

1. For Docker build/run, use `host.docker.internal` instead of `127.0.0.1`.
2. `127.0.0.1:7890` points to container itself, not Windows host.

### Quick start (recommended)

```bash
export ENABLE_CLASH_PROXY=1
export CLASH_PROXY_PORT=7890
./scripts/dev-shell.sh
```

This enables proxy envs for both `docker build` and `docker run`.

### Manual mode (if you want full control)

```bash
export HTTP_PROXY=http://host.docker.internal:7890
export HTTPS_PROXY=http://host.docker.internal:7890
export ALL_PROXY=http://host.docker.internal:7890
export NO_PROXY=localhost,127.0.0.1,::1,host.docker.internal
./scripts/dev-shell.sh
```

## Verify inside container

Inside dev shell:

```bash
env | grep -i proxy
apt-get update
```

If `apt-get update` succeeds, proxy path is working.

## Common issues

### Build still cannot reach network

1. Ensure Clash is running on Windows and port `7890` is open.
2. Ensure Docker Desktop is running and WSL integration is enabled.
3. Retry with explicit mode (`ENABLE_CLASH_PROXY=1`).
4. If your distro cannot resolve `host.docker.internal`, use Windows host IP explicitly:

```bash
export CLASH_PROXY_URL=http://<windows-host-ip>:7890
./scripts/dev-shell.sh
```
5. If `connect to host.docker.internal:7890` is refused, enable Clash `Allow LAN`
   (or equivalent) so the proxy is reachable from Docker/WSL network namespace.

### DNS works but HTTPS fails/timeouts

1. Switch from TUN mode to explicit proxy mode.
2. Confirm Clash node itself is healthy.

### Need to bypass some hosts

Append to `NO_PROXY`, e.g.:

```bash
export NO_PROXY="${NO_PROXY},mirrors.aliyun.com"
```
