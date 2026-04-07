#pragma once
#include <string>

namespace ReaClaw {
class DB;

namespace Catalog {

// Build or rebuild the action catalog in SQLite.
// Rebuilds only when the table is empty or the REAPER version has changed.
// Must be called from the main thread (uses kbd_enumerateActions).
void build(DB& db, const std::string& reaper_version);

// Return the number of indexed actions.
int  count(DB& db);

}  // namespace Catalog
}  // namespace ReaClaw
