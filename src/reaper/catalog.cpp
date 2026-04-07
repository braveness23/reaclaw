#include "reaper/catalog.h"
#include "db/db.h"
#include "util/logging.h"

// REAPER SDK — extern declarations of all API function pointers
// (REAPERAPI_IMPLEMENT is defined only in reaper/api.cpp)
#include <reaper_plugin_functions.h>

#include <string>

namespace ReaClaw::Catalog {

namespace {

std::string extract_category(const char* name) {
    // "Track: Toggle mute" → "Track"
    // Names without ": " separator fall into "General"
    for (int i = 0; name[i] != '\0' && i < 40; i++) {
        if (name[i] == ':' && name[i + 1] == ' ') {
            return std::string(name, i);
        }
    }
    return "General";
}

bool needs_rebuild(DB& db, const std::string& reaper_version) {
    if (db.scalar_int("SELECT COUNT(*) FROM actions") == 0) return true;
    return db.scalar_text("SELECT value FROM meta WHERE key='reaper_version'")
           != reaper_version;
}

}  // namespace

void build(DB& db, const std::string& reaper_version) {
    if (!needs_rebuild(db, reaper_version)) {
        Log::info("Catalog up to date: " +
                  std::to_string(db.scalar_int("SELECT COUNT(*) FROM actions")) +
                  " actions");
        return;
    }

    Log::info("Building action catalog...");

    KbdSectionInfo* section = SectionFromUniqueID(0);  // Main section
    if (!section) {
        Log::error("SectionFromUniqueID(0) returned null — cannot index actions");
        return;
    }

    db.execute("BEGIN TRANSACTION");
    db.execute("DELETE FROM actions");
    db.execute("DELETE FROM actions_fts");

    int indexed = 0;
    int idx     = 0;
    const char* name = nullptr;
    int cmd_id = 0;

    while ((cmd_id = kbd_enumerateActions(section, idx, &name)) != 0) {
        if (name && name[0] != '\0') {
            std::string category = extract_category(name);
            db.query(
                "INSERT OR REPLACE INTO actions(id, name, category, section) "
                "VALUES(?1, ?2, ?3, 'main')",
                {std::to_string(cmd_id), std::string(name), category});
            indexed++;
        }
        idx++;
    }

    // Rebuild FTS5 content index
    db.execute("INSERT INTO actions_fts(actions_fts) VALUES('rebuild')");

    db.execute("COMMIT");

    db.query("INSERT OR REPLACE INTO meta(key, value) VALUES('reaper_version', ?1)",
             {reaper_version});

    Log::info("Action catalog: indexed " + std::to_string(indexed) + " actions");
}

int count(DB& db) {
    return static_cast<int>(db.scalar_int("SELECT COUNT(*) FROM actions"));
}

}  // namespace ReaClaw::Catalog
