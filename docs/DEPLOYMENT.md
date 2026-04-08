# ReaClaw Deployment Guide

## Prerequisites

| Component | Requirement |
|-----------|-------------|
| REAPER | v6.0+ (v7.x recommended) |
| OpenSSL | 3.x (system package or vcpkg) |
| CMake | 3.20+ |
| Compiler | MSVC 2022+ (Windows), Clang 14+ (macOS), GCC 11+ / Clang 14+ (Linux) |
| luac | Optional — Lua 5.x for script syntax validation |

---

## Build

### Linux / macOS

```bash
git clone https://github.com/braveness23/reaclaw.git
cd reaclaw
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DREAPER_USER_PLUGINS="/path/to/UserPlugins"
cmake --build build --config Release
cmake --install build
```

**UserPlugins paths:**
- Linux: `~/.config/REAPER/UserPlugins/`
- macOS: `~/Library/Application Support/REAPER/UserPlugins/`

### Windows (MSVC)

Open a **Developer Command Prompt for VS 2022**, then:

```cmd
git clone https://github.com/braveness23/reaclaw.git
cd reaclaw
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
    -DREAPER_USER_PLUGINS="C:\Users\<you>\AppData\Roaming\REAPER\UserPlugins"
cmake --build build --config Release
cmake --install build --config Release
```

OpenSSL on Windows is easiest via vcpkg:
```cmd
vcpkg install openssl:x64-windows
```

### Build without installing

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# Output: build/reaper_reaclaw.{dll,dylib,so}
# Copy manually to REAPER's UserPlugins directory
```

---

## Installation

Copy the built extension to REAPER's `UserPlugins` directory:

| Platform | Path |
|----------|------|
| Windows | `%APPDATA%\REAPER\UserPlugins\reaper_reaclaw.dll` |
| macOS | `~/Library/Application Support/REAPER/UserPlugins/reaper_reaclaw.dylib` |
| Linux | `~/.config/REAPER/UserPlugins/reaper_reaclaw.so` |

Restart REAPER. On load, ReaClaw:
1. Creates `{ResourcePath}/reaclaw/` and writes `config.json` with defaults
2. Generates a self-signed TLS cert at `{ResourcePath}/reaclaw/certs/`
3. Opens `reaclawdb.sqlite` and runs schema migrations
4. Indexes REAPER's action catalog (~65K actions, takes ~1s)
5. Starts the HTTPS server on `0.0.0.0:9091`

You should see in the REAPER console:
```
ReaClaw [INFO ] [2026-04-07T...] ReaClaw 1.0.0 loading...
ReaClaw [INFO ] [2026-04-07T...] Database: /.../.config/REAPER/reaclaw/reaclawdb.sqlite
ReaClaw [INFO ] [2026-04-07T...] Catalog: indexed 65234 actions
ReaClaw [INFO ] [2026-04-07T...] HTTPS server listening on 0.0.0.0:9091
ReaClaw [INFO ] [2026-04-07T...] ReaClaw ready — https://0.0.0.0:9091
```

---

## Configuration

Config file: `{GetResourcePath()}/reaclaw/config.json`

Written with defaults on first run. Edit and restart REAPER to apply changes.

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 9091,
    "thread_pool_size": 4
  },
  "tls": {
    "enabled": true,
    "generate_if_missing": true,
    "cert_file": "",
    "key_file": ""
  },
  "auth": {
    "type": "api_key",
    "key": "sk_change_me"
  },
  "database": {
    "path": ""
  },
  "script_security": {
    "validate_syntax": true,
    "log_all_executions": true,
    "max_script_size_kb": 512
  },
  "logging": {
    "level": "info",
    "file": "",
    "format": "text"
  }
}
```

### Key options

| Field | Description |
|-------|-------------|
| `server.host` | Bind address. `"127.0.0.1"` for loopback-only; `"0.0.0.0"` for all interfaces (default) |
| `server.port` | HTTPS port (default 9091) |
| `auth.type` | `"api_key"` (recommended) or `"none"` (localhost dev only) |
| `auth.key` | Shared secret — **change this before exposing to any network** |
| `tls.cert_file` / `tls.key_file` | Paths to CA-signed cert/key; leave empty for auto-generated self-signed |
| `script_security.validate_syntax` | Run `luac -p` on scripts before registration (default true) |
| `script_security.max_script_size_kb` | Maximum Lua script body size (default 512 KB) |
| `logging.format` | `"text"` (default) or `"json"` for structured log output |
| `logging.file` | Append logs to this file path; empty = REAPER console only |

---

## First-run Verification

```bash
# Verify the server is up (self-signed cert — use -k)
curl -k -H "Authorization: Bearer sk_change_me" \
    https://localhost:9091/health

# Expected response:
{
  "status": "ok",
  "version": "1.0.0",
  "reaper_version": "7.x",
  "catalog_size": 65234,
  "uptime_seconds": 5,
  "queue_depth": 0,
  "db_ok": true,
  "server_running": true
}
```

---

## Security Checklist

- [ ] Change `auth.key` from `sk_change_me` to a random secret (e.g. `openssl rand -hex 32`)
- [ ] If exposing beyond localhost, restrict with a firewall rule or set `server.host` to `"127.0.0.1"`
- [ ] For CA-signed TLS, set `tls.cert_file` and `tls.key_file` (Let's Encrypt, local CA, etc.)
- [ ] Review `SECURITY.md` for the full threat model

---

## Updating

1. Build the new version
2. Quit REAPER
3. Copy the new `.dll`/`.dylib`/`.so` over the old one
4. Start REAPER

The database and config are preserved across updates. Schema migrations run automatically on open.

---

## Headless Testing on Linux

REAPER is a GUI application and normally requires a display. On a headless Linux server or CI machine you need a virtual display and a virtual audio device. This section documents exactly what worked on Ubuntu 24.04.

### System dependencies

Install everything with a single command:

```bash
sudo apt install -y \
    xvfb \
    libgtk-3-0 \
    libgdk-pixbuf2.0-0 \
    libasound2t64 \
    lua5.4
```

| Package | Why |
|---------|-----|
| `xvfb` | Virtual X11 framebuffer — REAPER needs a display even headless |
| `libgtk-3-0` | REAPER's Linux UI toolkit (libSwell links against GTK3) |
| `libgdk-pixbuf2.0-0` | GTK3 image loading dependency |
| `libasound2t64` | ALSA audio library (Ubuntu 24.04 renamed from `libasound2`) |
| `lua5.4` | Optional — enables Lua syntax validation on script registration |

> **Ubuntu version note:** On Ubuntu 22.04 and earlier, the ALSA package is named `libasound2` (no `t64` suffix).

### Virtual audio device

REAPER will block at audio initialization without an audio device. Load the ALSA dummy kernel module to create virtual sound cards:

```bash
sudo modprobe snd-dummy
```

Verify it worked — you should see new devices:

```bash
ls /dev/snd/
# Should include: controlC0  pcmC0D0c  pcmC0D0p  seq  timer
```

To make this persist across reboots:

```bash
echo "snd-dummy" | sudo tee /etc/modules-load.d/snd-dummy.conf
```

### REAPER audio pre-configuration

Pre-configure REAPER to use the null audio device before first launch so it doesn't prompt for audio setup:

```bash
mkdir -p ~/.config/REAPER
cat >> ~/.config/REAPER/reaper.ini << 'EOF'

[reaper]
audiodev=null
audiopref_alsa_dev=null
audiopref_audiodevclose_onhidden=0
mixwnd_vis=1
renderclosewhendone=4
EOF
```

> If `reaper.ini` already exists with a `[reaper]` section, edit it manually to add the `audiodev=null` and `audiopref_alsa_dev=null` lines.

### Pre-create the ReaClaw config with file logging

By default ReaClaw logs to the REAPER console (a GUI element). On a headless machine, pre-create the config with a log file path so you can see what the plugin is doing:

```bash
mkdir -p ~/.config/REAPER/reaclaw/certs ~/.config/REAPER/reaclaw/scripts
cat > ~/.config/REAPER/reaclaw/config.json << 'EOF'
{
    "server":  { "host": "0.0.0.0", "port": 9091, "thread_pool_size": 4 },
    "tls":     { "enabled": true, "generate_if_missing": true, "cert_file": "", "key_file": "" },
    "auth":    { "type": "api_key", "key": "sk_change_me" },
    "database": { "path": "" },
    "script_security": { "validate_syntax": true, "log_all_executions": true, "max_script_size_kb": 512 },
    "logging": { "level": "debug", "file": "/tmp/reaclaw.log", "format": "text" }
}
EOF
```

### Launching REAPER headlessly

Start Xvfb (virtual display), then start REAPER pointing at it:

```bash
# Start virtual display (only needed once per session)
Xvfb :99 -screen 0 1024x768x24 &

# Launch REAPER on that display
DISPLAY=:99 /opt/reaper/reaper -nosplash -newinst &

# Wait for startup (~5–10 seconds), then verify the server is up
sleep 10
curl -sk -H "Authorization: Bearer sk_change_me" https://127.0.0.1:9091/health
```

Watch the ReaClaw log in real time:

```bash
tail -f /tmp/reaclaw.log
```

Expected output once running:

```
ReaClaw [INFO ] [2026-04-08T00:55:37Z] Database: /home/.../.config/REAPER/reaclaw/reaclawdb.sqlite
ReaClaw [INFO ] [2026-04-08T00:55:37Z] Building action catalog...
ReaClaw [INFO ] [2026-04-08T00:55:37Z] Action catalog: indexed 4 actions
ReaClaw [INFO ] [2026-04-08T00:55:38Z] Generating self-signed TLS certificate (RSA-4096, 10yr)...
ReaClaw [INFO ] [2026-04-08T00:55:38Z] ReaClaw ready — https://0.0.0.0:9091
ReaClaw [INFO ] [2026-04-08T00:55:38Z] HTTPS server listening on 0.0.0.0:9091
```

### Confirming REAPER fully initialized

Use REAPER's `-splashlog` flag to write the internal startup log to a file:

```bash
DISPLAY=:99 /opt/reaper/reaper -nosplash -newinst -splashlog /tmp/reaper_splash.log &
sleep 10
cat /tmp/reaper_splash.log
```

REAPER is fully up when you see `splash window destroyed` at the end of the log. If the log stops at `Loading...` with `(empty)`, the audio device isn't resolving — recheck `snd-dummy` and `reaper.ini`.

### Shutting down

```bash
pkill reaper
pkill Xvfb
```

### Integration test script

Once REAPER is running headlessly, run the full integration test:

```bash
API="https://127.0.0.1:9091"
KEY="sk_change_me"   # match auth.key in config.json

# Health check
curl -sk -H "Authorization: Bearer $KEY" "$API/health" | python3 -m json.tool

# Catalog search
curl -sk -H "Authorization: Bearer $KEY" "$API/catalog/search?q=video" | python3 -m json.tool

# Current REAPER state
curl -sk -H "Authorization: Bearer $KEY" "$API/state" | python3 -m json.tool

# Execute an action (Video: Fullscreen = action 50122 on a stock install)
curl -sk -X POST -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
    -d '{"id": 50122, "agent_id": "test"}' "$API/execute/action" | python3 -m json.tool

# Register and execute a Lua script
curl -sk -X POST -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
    -d '{"name":"hello","script":"reaper.ShowConsoleMsg(\"hello\\n\")"}' \
    "$API/scripts/register" | python3 -m json.tool
```

### Known issues / platform notes

| Issue | Cause | Fix |
|-------|-------|-----|
| REAPER hangs at startup | No audio device | Load `snd-dummy` kernel module |
| `libSwell.so: undefined symbol: gdk_init_check` | GTK3 missing | `sudo apt install libgtk-3-0` |
| `libasound.so.2: cannot open shared object` | ALSA missing (Ubuntu 24.04) | `sudo apt install libasound2t64` |
| Port 9091 never appears | REAPERAPI_LoadAPI check failed (pre-v1.0.1) | Update to v1.0.1+ |
| Script execute returns 404 | Old build without `reaper_cmd_id` column | Update to v1.0.1+; delete `reaclawdb.sqlite` to force schema migration |
| `reaper: activating running instance` | REAPER detected an existing instance | Use `-newinst` flag, or `pkill -9 reaper` first |

---

## Troubleshooting

### Server doesn't start

Check the REAPER console for `ReaClaw [ERROR]` lines. Common causes:
- TLS cert generation failed (OpenSSL not linked, or certs directory not writable)
- Port 9091 already in use — change `server.port` in config

### Auth failures in logs

`Auth: invalid API key from 127.0.0.1` — the client is using the wrong key. Verify `auth.key` in config matches the `Authorization: Bearer ...` header.

### Catalog not indexed / empty search results

Catalog rebuilds when REAPER's version changes or when the actions table is empty. Check:
```bash
curl -k -H "Authorization: Bearer <key>" https://localhost:9091/health
# catalog_size should be > 0
```

If it's 0, check for `[ERROR]` lines during startup. `kbd_enumerateActions` and `SectionFromUniqueID` must be non-null.

### Script registration fails

- **Syntax error**: `luac` found a problem — fix the script before retrying
- **REAPER rejected**: `AddRemoveReaScript` returned 0 — ensure the scripts directory is inside REAPER's resource path and is writable
- **luac not found**: validation is skipped; install Lua (`sudo apt install lua5.4` / `brew install lua`) for syntax checking

### Logs

Default: REAPER console (`View → ReaScript Development Environment → Console`)

File logging: set `logging.file` in config to an absolute path.

JSON logging (for log aggregation tools):
```json
"logging": { "format": "json", "file": "/var/log/reaclaw.log" }
```

Output format:
```json
{"level":"info","ts":"2026-04-07T12:00:00Z","msg":"HTTPS server listening on 0.0.0.0:9091"}
```
