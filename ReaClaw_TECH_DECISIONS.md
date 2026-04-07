# ReaClaw: Technical Decisions

Key architectural decisions and rationale. Read this before modifying the design.

---

## 1. Implementation Type: Native C++ REAPER Extension

**Decision:** ReaClaw is a native REAPER extension (`.dll` / `.dylib` / `.so`), not an external process.

**Rationale:**
- A native extension runs inside REAPER's process and has direct access to the full REAPER C++ SDK via `GetFunc()` — every function REAPER exposes is available.
- No bridge scripts. No scraping REAPER's web interface. No limitations on what can be queried or controlled.
- Script registration via `AddRemoveReaScript()` is a direct API call — no workarounds needed.
- REAPER loads the extension automatically at startup from the `UserPlugins` directory.
- Full action enumeration via `kbd_enumerateActions()` — access to all 65K+ commands.

**Alternatives Rejected:**
- External Go/Python/Rust process calling REAPER's web interface: Limited to what REAPER's HTTP interface exposes (action execution only; no rich state, no script registration). Requires a "bridge" ReaScript, which is a workaround for a problem that doesn't exist in a native extension.

**Trade-offs:** None significant. C++ is the correct choice for REAPER extensions.

---

## 2. Language: C++17

**Decision:** C++17 standard.

**Rationale:**
- REAPER itself is C++. The SDK headers and WDL use C++.
- On Windows, MSVC is required because REAPER uses pure virtual interface classes with a specific ABI; other compilers are incompatible on Windows.
- C++17 gives us `std::filesystem`, `std::optional`, `std::string_view`, structured bindings — reduces boilerplate without requiring third-party utilities.
- All chosen vendor libraries (cpp-httplib, nlohmann/json) work with C++17.

**Platform compilers:**
- Windows: MSVC (required — ABI compatibility)
- macOS: Clang (Xcode)
- Linux: GCC or Clang

---

## 3. Embedded HTTP/HTTPS Server: cpp-httplib

**Decision:** [cpp-httplib](https://github.com/yhirose/cpp-httplib) (yhirose/cpp-httplib)

**Rationale:**
- **Header-only** — a single `httplib.h` file. Drop it in `vendor/` and include it. No separate build step, no linking.
- **HTTPS via OpenSSL** — same library handles both HTTP and HTTPS with identical API; just pass cert/key paths to `SSLServer`.
- **Cross-platform** — works identically on Windows, macOS, and Linux.
- **Thread pool** — built-in concurrent request handling; configurable thread count.
- **Actively maintained** — 13K+ GitHub stars, production use in many projects.
- **Simple API** — `server.Get("/path", handler)` is all it takes to register a route.

**Alternatives Rejected:**
- **Boost.Beast:** Full HTTP/WebSocket framework, but requires pulling in all of Boost. Massive dependency for an embedded server use case.
- **Crow:** Good framework but requires separate build, adds complexity for cross-platform extension.
- **Pistache:** Linux-only. Not cross-platform.
- **Custom raw sockets:** Not maintainable; reinventing the wheel.

**Trade-offs:**
- cpp-httplib compiles the server into the extension binary; this is acceptable and expected.
- Thread pool runs on background threads — requires careful use of the REAPER API (see Threading Model decision).

---

## 4. TLS: OpenSSL

**Decision:** OpenSSL for all TLS operations.

**Rationale:**
- cpp-httplib's HTTPS support is built on OpenSSL — no separate TLS library needed; they share a dependency.
- OpenSSL is available on all target platforms via system packages or vcpkg.
- Supports both self-signed certificate generation (for local/home-network use) and CA-signed certificates (for production).
- ReaClaw generates a self-signed cert on first run if none is configured — zero-friction startup.

**Self-signed cert generation:**
- Use `EVP_PKEY_keygen` + `X509` APIs directly in C++ to generate cert/key at startup.
- Store in `{REAPER_RESOURCE_PATH}/reaclaw/certs/` (outside version control).

**CA cert support:**
- User points `tls.cert_file` and `tls.key_file` in config to their own certificate files.
- Standard PEM format.

---

## 5. Persistence: SQLite Amalgamation

**Decision:** SQLite, distributed as the official C amalgamation (`sqlite3.c` + `sqlite3.h`).

**Rationale:**
- **Zero infrastructure** — single file database; no server process.
- **Bundled as source** — the SQLite amalgamation is two files added directly to `vendor/`. No external dependency, no package manager needed, no linking surprises.
- **ACID** — reliable transactions; safe for concurrent reads from the web server thread.
- **Fast for this workload** — catalog lookups, script caching, workflow storage, execution history are all read-heavy, low-volume workloads that SQLite handles well.
- **Easy backup** — copy `reaclawdb.sqlite` and you have a full backup.

**Alternatives Rejected:**
- PostgreSQL: Requires a server process. Overkill for a single-user REAPER plugin.
- JSON files: No querying, no transactions, not safe for concurrent access.
- In-memory only: State lost on REAPER restart.

**Trade-offs:**
- SQLite's single-writer model is fine here; concurrent writes from the web server are serialized through the command queue anyway.

---

## 6. JSON: nlohmann/json

**Decision:** [nlohmann/json](https://github.com/nlohmann/json) (single header `json.hpp`)

**Rationale:**
- Header-only — drop `json.hpp` in `vendor/` and include it. Nothing to build or link.
- Intuitive API: `json["key"] = value` reads and writes like a map.
- Handles all API request/response serialization and config file parsing.
- Well-documented and widely used in C++ projects.

**Alternatives Rejected:**
- RapidJSON: Faster but far more verbose API.
- Manual JSON: Not maintainable.
- YAML (yaml-cpp): Would require building a library. nlohmann/json is sufficient and simpler.

**Config format:** JSON (not YAML) for the same reason — nlohmann/json handles it without adding a YAML parser dependency.

---

## 7. Build System: CMake

**Decision:** CMake 3.20+

**Rationale:**
- De facto standard for cross-platform C++ projects.
- All major IDEs (Visual Studio, VS Code, CLion, Xcode) have CMake support.
- Works with MSVC, Clang, and GCC transparently.
- vcpkg integration for OpenSSL via `CMAKE_TOOLCHAIN_FILE`.
- Supports `install` target that copies the built `.dll`/`.dylib`/`.so` directly to the REAPER `UserPlugins` directory.
- Easy CI/CD integration (GitHub Actions, etc.).

**Build configuration:**
- Single `CMakeLists.txt` at project root.
- Vendor libraries included via `add_subdirectory` or direct source inclusion (for amalgamations).
- OpenSSL found via `find_package(OpenSSL REQUIRED)`.
- Platform-specific output filename handled by CMake: `reaper_reaclaw` with appropriate extension per platform.

---

## 8. Threading Model: Command Queue

**Decision:** Background server thread with a main-thread command queue for non-threadsafe REAPER calls.

**Rationale:**
The REAPER SDK distinguishes between threadsafe and non-threadsafe API functions. Most execution functions (`Main_OnCommand`, `Main_OnCommandEx`) must be called from REAPER's main thread. State query functions (track enumeration, BPM, transport) are threadsafe and can be called from any thread.

Architecture:
- `cpp-httplib` runs its thread pool on background threads.
- Threadsafe REAPER calls (state reads) are made directly from handler threads.
- Non-threadsafe calls (action execution) are posted to a `std::queue<Command>` protected by `std::mutex`.
- A REAPER timer callback registered via `plugin_register("timer", ...)` drains the queue on the main thread at ~30fps.
- Each queued command carries a `std::promise<Result>`; the handler thread waits on the corresponding `std::future` with a configurable timeout (default: 5 seconds).

**Trade-offs:**
- Adds ~33ms maximum latency for action execution (one timer tick). Acceptable for this use case.
- State queries have zero extra latency (called directly).

---

## 9. REAPER API Binding Strategy

**Decision:** Load all needed REAPER API functions at startup via `rec->GetFunc(name)` and store as function pointers in a global `ReaperAPI` struct.

**Rationale:**
- REAPER SDK provides `reaper_plugin_functions.h` which declares all available functions.
- Using `#define REAPERAPI_IMPLEMENT` before including `reaper_plugin_functions.h` initializes all pointers automatically.
- This is the canonical approach used by SWS and other major extensions.
- Centralizing API access in one struct makes it easy to mock for testing.

**Key functions used:**
- `kbd_enumerateActions` — build action catalog
- `Main_OnCommand` / `Main_OnCommandEx` — execute actions (main thread only)
- `AddRemoveReaScript` — register/unregister ReaScripts
- `CountTracks` / `GetTrack` / `GetTrackName` — track enumeration
- `GetProjectTimeSignature2` — BPM
- `GetPlayState` — transport state
- `GetResourcePath` — REAPER resource directory (for config, certs, DB)

---

## 10. Plugin Naming and Location

**Decision:** Plugin named `reaper_reaclaw` with platform-appropriate extension.

**Rationale:**
- REAPER extensions conventionally use the `reaper_` prefix (e.g., `reaper_mp3`, `reaper_sws`).
- The prefix signals to REAPER that this is an extension (not a VST/CLAP plugin).
- Platform suffixes are handled by CMake automatically.

**Output files:**
- Windows: `reaper_reaclaw.dll`
- macOS: `reaper_reaclaw.dylib`
- Linux: `reaper_reaclaw.so`

**Install path:**
- Windows: `%APPDATA%\REAPER\UserPlugins\`
- macOS: `~/Library/Application Support/REAPER/UserPlugins/`
- Linux: `~/.config/REAPER/UserPlugins/`

---

## 11. Config Location

**Decision:** Config file at `{GetResourcePath()}/reaclaw/config.json`

**Rationale:**
- `GetResourcePath()` is a REAPER SDK function that returns the user's REAPER config directory, regardless of platform.
- Storing ReaClaw's config inside the REAPER resource path keeps everything in one place alongside other REAPER configs.
- JSON format (nlohmann/json) — no extra parser dependency.
- ReaClaw creates the `reaclaw/` subdirectory if it does not exist on first run.
- If `config.json` is missing, ReaClaw writes defaults and logs a notice.

---

## 12. Script Security Model

**Decision:** Generated scripts get syntax validation and static analysis; execution is always logged; human approval is optional.

**Rationale:**
- Built-in REAPER actions and community scripts (ReaPack) are fully trusted — no validation.
- AI-generated ReaScripts are new code and should be checked before registration.
- Lua syntax validation: Invoke `luac -p` (if available) or use a lightweight Lua parser.
- Static analysis: Scan for patterns that are suspicious in this context (`os.execute`, `io.open` outside project dir, `reaper.ExecProcess`). Return warnings, not hard failures.
- Approval gate is optional (config: `script_security.require_approval`). Default off for smooth agent operation.
- Every execution — generated or not — is written to the SQLite audit log. If something goes wrong, the full trace is available.

---

## 13. Agent Independence

**Decision:** ReaClaw has zero dependencies on OpenClaw, Sparky, or any specific AI provider.

**Rationale:**
- Any HTTP client can call the API: curl, Claude tool use, OpenAI function calling, custom code.
- No vendor-specific auth or protocol.
- MCP wrapper (Phase 4) is an optional thin layer on top, not a requirement.
- LLM for script generation is configurable: Anthropic, OpenAI, or any OpenAI-compatible endpoint (including LiteLLM proxies).

---

## Summary

| Concern | Decision |
|---|---|
| Extension type | Native C++ REAPER extension |
| Language | C++17 |
| HTTP server | cpp-httplib (header-only) |
| TLS | OpenSSL |
| Database | SQLite amalgamation (bundled) |
| JSON | nlohmann/json (header-only) |
| Build | CMake 3.20+ |
| Threading | Background server + main-thread command queue |
| REAPER API | GetFunc binding via reaper_plugin_functions.h |
| Config | JSON at GetResourcePath()/reaclaw/config.json |
| Naming | reaper_reaclaw.{dll,dylib,so} |
