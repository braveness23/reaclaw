#include "handlers/guide.h"

#include "agent_guide.gen.h"
#include "handlers/common.h"

#include <httplib.h>

#include <string>

#include <json.hpp>

// Defined via CMake for the extension target; tests compile this file without it.
#ifndef REACLAW_VERSION
#define REACLAW_VERSION "dev"
#endif

namespace ReaClaw::Handlers {

void handle_agent_guide(const httplib::Request& req, httplib::Response& res) {
    std::string md(reinterpret_cast<const char*>(agent_guide_md), agent_guide_md_len);

    if (req.get_param_value("format") == "json") {
        nlohmann::json j = {{"version", REACLAW_VERSION},
                            {"markdown", md},
                            {"links",
                             {{"capabilities", "GET /capabilities"},
                              {"catalog_search", "GET /catalog/search?q="},
                              {"recipes", "GET /recipes"},
                              {"events", "GET /events?since="},
                              {"changes", "GET /state/changes"}}}};
        json_ok(res, j);
        return;
    }

    res.status = 200;
    res.set_content(md, "text/markdown; charset=utf-8");
}

}  // namespace ReaClaw::Handlers
