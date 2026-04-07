#include "handlers/scripts.h"
#include "handlers/common.h"

#include <httplib.h>

// Script management is implemented in Phase 1.
// These stubs return 501 so the router can register all routes
// and the CI build succeeds for Phase 0.

namespace ReaClaw::Handlers {

void handle_scripts_register(const httplib::Request&, httplib::Response& res) { not_implemented(res); }
void handle_scripts_cache   (const httplib::Request&, httplib::Response& res) { not_implemented(res); }
void handle_scripts_get     (const httplib::Request&, httplib::Response& res) { not_implemented(res); }
void handle_scripts_delete  (const httplib::Request&, httplib::Response& res) { not_implemented(res); }

}  // namespace ReaClaw::Handlers
