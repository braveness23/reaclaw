# ReaClaw: Technical Decisions

Key architectural decisions and rationale. Read this before modifying the design.

---

## 1. Implementation Type: Native C++ REAPER Extension

**Decision:** ReaClaw is a native REAPER extension (`.dll` / `.dylib` / `.so`), not an external process.

**Rationale:**
- A native extension runs inside REAPER's process and has direct access to the full REAPER C++ SDK via `GetFunc()` — every function REAPER exposes is available.
- No bridge scripts. No scraping REAPER's web interface. No limitations on what can be queried or controlled.
- Script registration via `AddRemoveReaScript()` is a direct API call.
- Full action enumeration via `kbd_enumerateActions()` — all 65K+ commands.
- REAPER loads the extension automatically at startup from the `UserPlugins` directory.

**Alternatives Rejected:**
- External Go/Python/Rust process calling REAPER's built-in web interface: Limited to what REAPER's HTTP interface exposes. Requires a "bridge" ReaScript workaround. No native script registration. Wrong architecture for this problem.

---

## 2. Language: C++17

**Decision:** C++17 standard.

**Rationale:**
- REAPER itself is C++. The SDK headers and WDL use C++.
- On Windows, MSVC is required because REAPER uses pure virtual interface classes with a specific ABI; other compilers are incompatible on Windows.
- C++17 gives us `std::filesystem`, `std::optional`, `std::string_view`, structured bindings — reduces boilerplate without extra libraries.
- All chosen vendor libraries (cpp-httplib, nlohmann/json) work with C++17.

**Platform compilers:**
- Windows: MSVC (required — ABI compatibility)
- macOS: Clang (Xcode)
- Linux: GCC or Clang

---

## 3. Embedded HTTP/HTTPS Server: cpp-httplib

**Decision:** [cpp-httplib](https://github.com/yhirose/cpp-httplib)

**Rationale:**
- **Header-only** — a single `httplib.h`. Drop it in `vendor/` and include it. No separate build step.
- **HTTPS via OpenSSL** — same library handles both HTTP and HTTPS; just pass cert/key paths to `SSLServer`.
- **Cross-platform** — works identically on Windows, macOS, Linux.
- **Thread pool** — built-in concurrent request handling.
- **Actively maintained** — 13K+ GitHub stars, production use in many projects.

**Alternatives Rejected:**
- **Boost.Beast:** Requires pulling in all of Boost. Overkill.
- **Crow:** Separate build required; more complexity.
- **Pistache:** Linux-only.

---

## 4. TLS: OpenSSL

**Decision:** OpenSSL for all TLS operations.

**Rationale:**
- cpp-httplib's HTTPS support is built on OpenSSL — shared dependency, no extra library.
- Available on all target platforms via system packages or vcpkg.
- Supports both self-signed (auto-generated on first run) and CA-signed certificates.

**Self-signed cert generation:** Use `EVP_PKEY_keygen` + X509 APIs in C++ at startup if no cert is configured. Stored in `{ResourcePath}/reaclaw/certs/`.

---

## 5. Persistence: SQLite Amalgamation

**Decision:** SQLite, distributed as the official C amalgamation (`sqlite3.c` + `sqlite3.h`).

**Rationale:**
- **Zero infrastructure** — single file database; no server process.
- **Bundled as source** — two files added directly to `vendor/`. No external dependency.
- **ACID** — reliable transactions; safe for concurrent reads.
- **Fast** — catalog lookups, script caching, and execution history are read-heavy, low-volume workloads SQLite handles well.
- **Easy backup** — copy one file.

SQLite is always sufficient for this use case. ReaClaw is a single-machine REAPER extension; there is no multi-server scenario.

---

## 6. JSON: nlohmann/json

**Decision:** [nlohmann/json](https://github.com/nlohmann/json) (single header `json.hpp`)

**Rationale:**
- Header-only — drop in `vendor/` and include.
- Handles all API request/response serialization and config file parsing.
- Intuitive API: `json["key"] = value`.

**Config format:** JSON, not YAML. nlohmann/json handles it; a YAML parser (yaml-cpp) would require building a separate library.

---

## 7. Build System: CMake

**Decision:** CMake 3.20+

**Rationale:**
- De facto standard for cross-platform C++ projects.
- Supported by all major IDEs (Visual Studio, VS Code, CLion, Xcode).
- Works with MSVC, Clang, and GCC.
- vcpkg integration for OpenSSL via `CMAKE_TOOLCHAIN_FILE`.
- `install` target copies the built extension directly to the REAPER `UserPlugins` directory.

---

## 8. Threading Model: Command Queue

**Decision:** Background server thread pool with a main-thread command queue for non-threadsafe REAPER calls.

**Rationale:**
The REAPER SDK distinguishes threadsafe from non-threadsafe API functions. `Main_OnCommand` (action execution) must be called from REAPER's main thread. Most state reads (`CountTracks`, `GetTrack`, etc.) are threadsafe.

- cpp-httplib runs its thread pool on background threads.
- Threadsafe state reads are made directly from handler threads — zero added latency.
- Non-threadsafe calls are posted to a `std::queue<Command>` protected by `std::mutex`.
- A REAPER timer callback (registered via `plugin_register("timer", ...)`) drains the queue on the main thread at ~30fps.
- Each queued command carries a `std::promise<Result>`; the handler blocks on the `std::future` with a 5s timeout.

**Trade-off:** ~33ms maximum added latency for action execution (one timer tick). Acceptable for this use case.

---

## 9. REAPER API Binding Strategy

**Decision:** Load all needed REAPER API functions at startup via `rec->GetFunc(name)` using `#define REAPERAPI_IMPLEMENT` before including `reaper_plugin_functions.h`.

**Rationale:**
- This is the canonical approach used by SWS and other major REAPER extensions.
- Populates global function pointers for all REAPER API functions in one call (`REAPERAPI_LoadAPI`).
- Log a warning for any pointer that comes back null (indicates older REAPER version missing that API).

**Key functions used:**
- `kbd_enumerateActions`, `SectionFromUniqueID` — catalog
- `Main_OnCommand`, `Main_OnCommandEx` — execution (main thread only)
- `AddRemoveReaScript` — script registration
- `CountTracks`, `GetTrack`, `GetTrackName`, `GetSetMediaTrackInfo` — track state
- `GetProjectTimeSignature2`, `GetCursorPosition`, `GetPlayState` — project state
- `GetResourcePath`, `GetAppVersion`, `ShowConsoleMsg` — utility

---

## 10. Script Validation: Syntax Only

**Decision:** Validate Lua syntax before registration. No static analysis. No approval gate.

**Rationale:**
- The agent submitting a script is a trusted AI system — that's the entire premise of ReaClaw.
- Syntax validation exists to catch generation errors (malformed output, truncated response) before they reach REAPER, not to second-guess the agent's intent.
- Static analysis (warning on `os.execute`, etc.) is noise when the agent put it there intentionally.
- An approval gate defeats automation entirely.

**Implementation:** Shell out to `luac -p {file}` if available; lightweight fallback otherwise. Return error with line number on failure; do not register.

---

## 11. No LLM Client in the Extension

**Decision:** ReaClaw does not call any LLM API. The agent generates scripts; ReaClaw registers them.

**Rationale:**
- The agent calling ReaClaw is already an LLM. Having the extension call an LLM on behalf of an LLM is architectural redundancy.
- Removing the LLM client eliminates: outbound HTTP code, API key management in the extension, provider abstraction (Anthropic/OpenAI/etc.), and the associated config section.
- The correct flow: agent generates Lua → `POST /scripts/register` → ReaClaw validates syntax and calls `AddRemoveReaScript`.

---

## 12. Authentication: Two Modes Only

**Decision:** `none` or `api_key`. No mTLS.

**Rationale:**
- ReaClaw runs on the same machine as REAPER. The realistic auth scenarios are:
  - `none`: localhost-only access, trusted environment
  - `api_key`: home network or any case where a simple shared secret is sufficient
- mTLS (client certificates, CA setup) is never going to be used for a personal REAPER extension. It adds certificate management complexity with no practical benefit.

---

## 13. No Rate Limiting

**Decision:** No rate limiting.

**Rationale:**
- ReaClaw is a single-user tool. One person and their AI agent use it.
- Rate limiting defends against external attackers abusing the API. If attackers can reach your REAPER extension's port, rate limiting is not the right defense — network isolation is.
- Removing it eliminates middleware code, per-IP sliding window tracking, and config options.

---

## 14. Plugin Naming and Location

**Decision:** Plugin named `reaper_reaclaw` with platform-appropriate extension.

**Rationale:** REAPER extensions conventionally use the `reaper_` prefix (e.g., `reaper_mp3`, `reaper_sws`), signaling to REAPER that this is an extension rather than a VST/CLAP plugin.

**Output files:**
- Windows: `reaper_reaclaw.dll`
- macOS: `reaper_reaclaw.dylib`
- Linux: `reaper_reaclaw.so`

**Install path:**
- Windows: `%APPDATA%\REAPER\UserPlugins\`
- macOS: `~/Library/Application Support/REAPER/UserPlugins/`
- Linux: `~/.config/REAPER/UserPlugins/`

---

## 15. Config Location

**Decision:** `{GetResourcePath()}/reaclaw/config.json`

**Rationale:** `GetResourcePath()` returns the user's REAPER config directory on all platforms. Storing ReaClaw's config there keeps everything alongside other REAPER configs. ReaClaw creates the `reaclaw/` subdirectory and writes defaults if the file is missing.

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
| Script validation | Lua syntax check only |
| LLM | None — agent generates, extension registers |
| Auth | none or api_key |
| Rate limiting | None |
| Config | JSON at GetResourcePath()/reaclaw/config.json |
| Plugin name | reaper_reaclaw.{dll,dylib,so} |
