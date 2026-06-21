#pragma once
#include <string>

namespace ReaClaw {
class DB;

namespace Catalog {

// Build or rebuild the action catalog in SQLite.
// Rebuilds when the table is empty, the REAPER version changed, or the catalog
// builder version changed.
// Must be called from the main thread (uses kbd_getTextFromCmd / kbd_enumerateActions).
void build(DB& db, const std::string& reaper_version);

// Return the number of indexed actions.
int count(DB& db);

// Look up an action's display name by command id (e.g. "Track: Insert new track").
// Returns "" when the id is unknown. DB read only — safe on any thread; does not
// touch the REAPER SDK.
std::string action_name(DB& db, int cmd_id);

}  // namespace Catalog
}  // namespace ReaClaw
