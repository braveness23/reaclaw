#include "reaper/catalog.h"

#include "db/db.h"
#include "reaper/native_actions.gen.h"
#include "util/logging.h"

// REAPER SDK — extern declarations of all API function pointers
// (REAPERAPI_IMPLEMENT is defined only in reaper/api.cpp)
#include <string>

#include <reaper_plugin_functions.h>

namespace ReaClaw::Catalog {

namespace {

// Bump whenever build() changes how the catalog is populated, so existing
// installs (whose catalog was built by an older version) rebuild on next load
// even when the REAPER version is unchanged.
constexpr int k_catalog_version = 3;

// Highest native main-section command ID to probe. REAPER's built-in action
// IDs fit within a 16-bit range; scanning to this bound captures them all.
constexpr int k_max_native_cmd_id = 0xFFFF;

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
    if (db.scalar_int("SELECT COUNT(*) FROM actions") == 0)
        return true;
    if (db.scalar_text("SELECT value FROM meta WHERE key='reaper_version'") != reaper_version)
        return true;
    return db.scalar_text("SELECT value FROM meta WHERE key='catalog_version'") !=
           std::to_string(k_catalog_version);
}

}  // namespace

void build(DB& db, const std::string& reaper_version) {
    if (!needs_rebuild(db, reaper_version)) {
        Log::info("Catalog up to date: " +
                  std::to_string(db.scalar_int("SELECT COUNT(*) FROM actions")) + " actions");
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

    // Bundled-native pass: REAPER's built-in actions are not enumerable via the
    // SDK (kbd_getTextFromCmd / kbd_enumerateActions return nothing for them), so
    // seed the catalog from a static ID->name table. The live passes below then
    // overlay whatever extensions/scripts are actually installed.
    for (const auto& na : k_native_actions) {
        db.query(
                "INSERT OR REPLACE INTO actions(id, name, category, section) "
                "VALUES(?1, ?2, ?3, 'main')",
                {std::to_string(na.id), std::string(na.name), extract_category(na.name)});
    }

    // Live pass: resolve currently-registered command IDs (extensions, ReaPack,
    // ReaScripts) to their names via kbd_getTextFromCmd. This reflects whatever
    // is actually installed; it does NOT cover native built-ins (handled above).
    // Probe the full command-ID range; IDs fit within 16 bits.
    if (kbd_getTextFromCmd) {
        for (int cmd = 1; cmd <= k_max_native_cmd_id; cmd++) {
            const char* name = kbd_getTextFromCmd(cmd, section);
            if (name && name[0] != '\0') {
                db.query(
                        "INSERT OR REPLACE INTO actions(id, name, category, section) "
                        "VALUES(?1, ?2, ?3, 'main')",
                        {std::to_string(cmd), std::string(name), extract_category(name)});
            }
        }
    } else {
        Log::warn("kbd_getTextFromCmd unavailable — catalog limited to bound actions");
    }

    // Secondary pass: pick up bound custom/named actions (ReaScripts, extension
    // and user custom actions) whose command IDs fall outside the native range.
    int idx = 0;
    const char* en_name = nullptr;
    int cmd_id = 0;
    while ((cmd_id = kbd_enumerateActions(section, idx, &en_name)) != 0) {
        if (en_name && en_name[0] != '\0') {
            db.query(
                    "INSERT OR REPLACE INTO actions(id, name, category, section) "
                    "VALUES(?1, ?2, ?3, 'main')",
                    {std::to_string(cmd_id), std::string(en_name), extract_category(en_name)});
        }
        idx++;
    }

    // Rebuild FTS5 content index
    db.execute("INSERT INTO actions_fts(actions_fts) VALUES('rebuild')");

    db.execute("COMMIT");

    db.query("INSERT OR REPLACE INTO meta(key, value) VALUES('reaper_version', ?1)",
             {reaper_version});
    db.query("INSERT OR REPLACE INTO meta(key, value) VALUES('catalog_version', ?1)",
             {std::to_string(k_catalog_version)});

    int indexed = db.scalar_int("SELECT COUNT(*) FROM actions");
    Log::info("Action catalog: indexed " + std::to_string(indexed) + " actions");
}

int count(DB& db) {
    return static_cast<int>(db.scalar_int("SELECT COUNT(*) FROM actions"));
}

}  // namespace ReaClaw::Catalog
