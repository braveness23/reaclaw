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
# e.g.: curl -L https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip -o /tmp/sq.zip
#       unzip -j /tmp/sq.zip "*/sqlite3.c" "*/sqlite3.h" -d vendor/

# REAPER SDK
git clone https://github.com/justinfrankel/reaper-sdk.git vendor/reaper-sdk
# Copy the sdk/ headers into vendor/reaper-sdk/ (the include path CMake uses):
cp vendor/reaper-sdk/sdk/*.h vendor/reaper-sdk/   # if the clone lands in a subdir, adjust

# WDL/swell — required on Linux and macOS (reaper_plugin.h includes ../WDL/swell/swell.h)
git clone --depth=1 --filter=blob:none --sparse https://github.com/justinfrankel/WDL.git /tmp/wdl-tmp
cd /tmp/wdl-tmp && git sparse-checkout set WDL/swell && cd -
cp -r /tmp/wdl-tmp/WDL/swell vendor/WDL/
```
