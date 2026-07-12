// GET /agent/guide — the embedded onboarding manual must be present, intact,
// and match the committed docs/AGENT_GUIDE.md sections agents depend on.
#include "handlers/guide.h"

#include <httplib.h>

#include <gtest/gtest.h>
#include <json.hpp>

namespace {

httplib::Request make_req(const std::string& query_format = "") {
    httplib::Request req;
    req.method = "GET";
    req.path = "/agent/guide";
    if (!query_format.empty())
        req.params.emplace("format", query_format);
    return req;
}

TEST(AgentGuide, ServesMarkdown) {
    auto req = make_req();
    httplib::Response res;
    ReaClaw::Handlers::handle_agent_guide(req, res);

    EXPECT_EQ(res.status, 200);
    EXPECT_EQ(res.get_header_value("Content-Type"), "text/markdown; charset=utf-8");
    EXPECT_GT(res.body.size(), 1000u);
    // Sentinel sections agents are told to rely on.
    EXPECT_NE(res.body.find("latency contract"), std::string::npos);
    EXPECT_NE(res.body.find("/events/stream"), std::string::npos);
    EXPECT_NE(res.body.find("/state/changes"), std::string::npos);
    EXPECT_NE(res.body.find("GET /agent/guide"), std::string::npos);
}

TEST(AgentGuide, JsonFormatCarriesVersionAndLinks) {
    auto req = make_req("json");
    httplib::Response res;
    ReaClaw::Handlers::handle_agent_guide(req, res);

    EXPECT_EQ(res.status, 200);
    auto j = nlohmann::json::parse(res.body);
    EXPECT_TRUE(j.contains("version"));
    EXPECT_FALSE(j["version"].get<std::string>().empty());
    EXPECT_GT(j["markdown"].get<std::string>().size(), 1000u);
    EXPECT_EQ(j["links"]["capabilities"], "GET /capabilities");
    EXPECT_EQ(j["links"]["changes"], "GET /state/changes");
}

}  // namespace
