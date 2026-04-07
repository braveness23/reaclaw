# vendor/

Bundled third-party dependencies. All are header-only or single-file C libraries —
no submodules, no package manager required for the C++ sources themselves.

| File(s) | Library | Version | Source |
|---|---|---|---|
| `httplib.h` | cpp-httplib | latest | https://github.com/yhirose/cpp-httplib |
| `json.hpp` | nlohmann/json | 3.x | https://github.com/nlohmann/json |
| `sqlite3.c`, `sqlite3.h` | SQLite amalgamation | 3.x | https://www.sqlite.org/download.html |
| `reaper-sdk/` | REAPER Extension SDK | current | https://github.com/justinfrankel/reaper-sdk |

**OpenSSL** is a system dependency (not bundled). Install via:
- Linux: `sudo apt-get install libssl-dev`
- macOS: `brew install openssl@3`
- Windows: `vcpkg install openssl:x64-windows`

## Populating vendor/

These files are not committed to the repository. Run:

```bash
# cpp-httplib
curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
     -o vendor/httplib.h

# nlohmann/json (single header, 3.11.x)
curl -L https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp \
     -o vendor/json.hpp

# SQLite amalgamation — download from https://www.sqlite.org/download.html
# Extract sqlite3.c and sqlite3.h into vendor/

# REAPER SDK
git clone https://github.com/justinfrankel/reaper-sdk.git vendor/reaper-sdk
```
