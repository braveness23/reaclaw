#pragma once
#include <string>
#include <vector>

namespace ReaClaw::Scripts {

struct RegisterResult {
    bool registered = false;
    std::string action_id;
    std::string script_path;
    // Populated only when registered == false due to Lua syntax error
    int syntax_error_line = 0;
    std::string syntax_error_message;
    // General internal error message (non-syntax failures)
    std::string internal_error;
};

// Register an agent-provided Lua ReaScript as a custom REAPER action.
// Idempotent: if a script with the same name already exists, returns it unchanged.
// Validates Lua syntax via luac -p (if validate_syntax is enabled in config).
// Writes the file and calls AddRemoveReaScript on the REAPER main thread.
RegisterResult register_script(const std::string& name,
                               const std::string& body,
                               const std::vector<std::string>& tags);

// Remove a registered script from REAPER, disk, and the DB.
// Returns false if action_id is not found in the DB.
bool unregister_script(const std::string& action_id);

}  // namespace ReaClaw::Scripts
