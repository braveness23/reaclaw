# Contributing to ReaClaw

Thanks for your interest in contributing. ReaClaw is a native C++ REAPER extension — the bar for correctness is high because bugs can affect a running DAW session.

---

## Before You Start

- Check open issues and pull requests to avoid duplicate work.
- For significant changes, open an issue first to discuss the approach.
- Read `ReaClaw_Design.md` and `ReaClaw_TECH_DECISIONS.md` before touching architecture.

---

## Building

### Prerequisites

| Platform | Compiler | TLS |
|---|---|---|
| Windows | MSVC 2019+ (required for REAPER ABI) | vcpkg: `openssl` |
| macOS | Xcode Clang 14+ | Homebrew: `openssl@3` |
| Linux | GCC 10+ or Clang 14+ | `libssl-dev` (apt) or `openssl-devel` (dnf) |

CMake 3.20+ required on all platforms.

### Steps

```bash
# Clone
git clone https://github.com/braveness23/reaclaw.git
cd reaclaw

# Configure (Linux/macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Configure (Windows with vcpkg)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --config Release

# Install to REAPER UserPlugins (set REAPER_USER_PLUGINS to your path)
cmake --install build
```

### Vendor Dependencies

The following are bundled in `vendor/` and must be present:

| Library | How to get |
|---|---|
| `httplib.h` | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) — latest release |
| `json.hpp` | [nlohmann/json](https://github.com/nlohmann/json) — 3.x single header |
| `sqlite3.c` + `sqlite3.h` | [SQLite amalgamation](https://www.sqlite.org/download.html) |
| `reaper-sdk/` | [justinfrankel/reaper-sdk](https://github.com/justinfrankel/reaper-sdk) — copy `sdk/*.h` into `vendor/reaper-sdk/` |
| `WDL/swell/` | [justinfrankel/WDL](https://github.com/justinfrankel/WDL) — required on Linux and macOS (sparse clone `WDL/swell` into `vendor/WDL/`) |

Run the helper script to fetch everything at once:

```bash
bash scripts/fetch-vendor-deps.sh
```

Or see `vendor/README.md` for manual fetch commands. OpenSSL is a system/vcpkg dependency and is not bundled.

---

## Git Hooks (do this once)

```bash
scripts/install-git-hooks.sh
```

This points git at `.githooks/` and installs two hooks that call the exact same scripts CI runs, so a commit that passes locally passes in CI:

| Hook | Runs | Backed by |
|---|---|---|
| `pre-commit` | clang-format check (fast, every commit) | `scripts/checks/format.sh --check` |
| `pre-push` | full configure + build + ctest | `scripts/checks/build.sh`, `scripts/checks/test.sh` |

Bypass a one-off with `--no-verify` on either command. CI is still the actual merge gate and re-runs the same checks regardless — the hooks exist to fail fast, locally, before a broken commit ever reaches CI.

If you're changing a lint rule, compiler flag, or test invocation, edit the shared script under `scripts/checks/`, not the CI workflow or a hook directly — both call the same file, so editing only one side reintroduces drift.

---

## Code Style

All C++ is formatted with clang-format. Check or fix with the same script CI uses:

```bash
scripts/checks/format.sh --check   # what CI runs; exits nonzero on any diff
scripts/checks/format.sh --fix     # apply formatting in place
```

The `.clang-format` config at the repo root governs style (Google base, 4-space indent, 100 column limit). See `.editorconfig` for editor settings.

---

## Testing

- Unit tests live in `tests/`. Build and run with the shared scripts (same ones CI and the `pre-push` hook use):

```bash
scripts/checks/build.sh
scripts/checks/test.sh
```

- Integration tests require a running REAPER instance with ReaClaw loaded. See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for headless Linux setup instructions.
- All tests must pass before a PR is merged.

---

## Pull Requests

- Target the `main` branch.
- One logical change per PR — don't bundle unrelated fixes.
- Include a test for any new behavior.
- Keep commit history clean; squash fixup commits before requesting review.
- Fill out the PR template completely.

---

## Reporting Bugs

Use the GitHub issue tracker. Include:

- ReaClaw version (`GET /health` → `version` field)
- REAPER version and OS/platform
- Steps to reproduce
- Relevant logs (REAPER console output)

For security issues, follow [SECURITY.md](SECURITY.md).

---

## Code of Conduct

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
