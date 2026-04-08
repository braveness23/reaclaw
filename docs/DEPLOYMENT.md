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
