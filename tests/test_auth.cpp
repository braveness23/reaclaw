#include <gtest/gtest.h>
#include "auth/auth.h"
#include "config/config.h"
#include "util/logging.h"

#include <httplib.h>

class AuthTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        ReaClaw::Log::init(ReaClaw::LogLevel::error, "", nullptr);
    }

    ReaClaw::Config cfg;

    void SetUp() override {
        cfg.auth_type = "api_key";
        cfg.auth_key  = "test_secret_key";
    }

    httplib::Request make_req(const std::string& auth_header = "") {
        httplib::Request req;
        req.remote_addr = "127.0.0.1";
        if (!auth_header.empty()) {
            req.headers.emplace("Authorization", auth_header);
        }
        return req;
    }
};

TEST_F(AuthTest, CorrectKeyAccepted) {
    EXPECT_TRUE(ReaClaw::Auth::check(cfg, make_req("Bearer test_secret_key")));
}

TEST_F(AuthTest, WrongKeyRejected) {
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req("Bearer wrong_key")));
}

TEST_F(AuthTest, MissingAuthHeaderRejected) {
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req()));
}

TEST_F(AuthTest, BasicAuthRejected) {
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req("Basic dXNlcjpwYXNz")));
}

TEST_F(AuthTest, BearerWithEmptyKeyRejected) {
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req("Bearer ")));
}

TEST_F(AuthTest, BearerPrefixAloneRejected) {
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req("Bearer")));
}

TEST_F(AuthTest, AuthTypeNoneAlwaysPasses) {
    cfg.auth_type = "none";
    EXPECT_TRUE(ReaClaw::Auth::check(cfg, make_req()));
    EXPECT_TRUE(ReaClaw::Auth::check(cfg, make_req("Bearer wrong")));
}

TEST_F(AuthTest, KeyIsCaseSensitive) {
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req("Bearer TEST_SECRET_KEY")));
    EXPECT_FALSE(ReaClaw::Auth::check(cfg, make_req("Bearer Test_Secret_Key")));
}

TEST_F(AuthTest, RejectSets401WithJson) {
    httplib::Response res;
    ReaClaw::Auth::reject(res);
    EXPECT_EQ(res.status, 401);
    EXPECT_NE(res.body.find("UNAUTHORIZED"), std::string::npos);
}
