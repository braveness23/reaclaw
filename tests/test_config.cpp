#include <gtest/gtest.h>
#include "config/config.h"
#include "util/logging.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class ConfigTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        ReaClaw::Log::init(ReaClaw::LogLevel::error, "", nullptr);
    }

    fs::path tmp_dir;

    void SetUp() override {
        tmp_dir = fs::temp_directory_path() /
                  ("reaclaw_cfg_" + std::to_string(
                       std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(tmp_dir);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir);
    }
};

TEST_F(ConfigTest, DefaultsWrittenOnFirstRun) {
    ReaClaw::Config cfg;
    ASSERT_TRUE(ReaClaw::Config::load(cfg, tmp_dir.string()));

    EXPECT_TRUE(fs::exists(tmp_dir / "reaclaw" / "config.json"));

    EXPECT_EQ(cfg.host,              "0.0.0.0");
    EXPECT_EQ(cfg.port,              9091);
    EXPECT_EQ(cfg.auth_type,         "api_key");
    EXPECT_EQ(cfg.auth_key,          "sk_change_me");
    EXPECT_EQ(cfg.log_level,         "info");
    EXPECT_EQ(cfg.log_format,        "text");
    EXPECT_EQ(cfg.max_script_size_kb, 512);
    EXPECT_TRUE(cfg.validate_syntax);
    EXPECT_TRUE(cfg.log_all_executions);
}

TEST_F(ConfigTest, LoadsCustomValues) {
    fs::create_directories(tmp_dir / "reaclaw");
    std::ofstream f(tmp_dir / "reaclaw" / "config.json");
    f << R"({
        "server":  {"host": "127.0.0.1", "port": 9999, "thread_pool_size": 8},
        "auth":    {"type": "none", "key": "mykey"},
        "logging": {"level": "debug", "format": "json"}
    })";
    f.close();

    ReaClaw::Config cfg;
    ASSERT_TRUE(ReaClaw::Config::load(cfg, tmp_dir.string()));

    EXPECT_EQ(cfg.host,            "127.0.0.1");
    EXPECT_EQ(cfg.port,            9999);
    EXPECT_EQ(cfg.thread_pool_size, 8);
    EXPECT_EQ(cfg.auth_type,       "none");
    EXPECT_EQ(cfg.auth_key,        "mykey");
    EXPECT_EQ(cfg.log_level,       "debug");
    EXPECT_EQ(cfg.log_format,      "json");
}

TEST_F(ConfigTest, MissingFieldsUseDefaults) {
    fs::create_directories(tmp_dir / "reaclaw");
    std::ofstream f(tmp_dir / "reaclaw" / "config.json");
    f << R"({"server": {"port": 1234}})";
    f.close();

    ReaClaw::Config cfg;
    ASSERT_TRUE(ReaClaw::Config::load(cfg, tmp_dir.string()));

    EXPECT_EQ(cfg.port,              1234);
    EXPECT_EQ(cfg.host,              "0.0.0.0");   // default preserved
    EXPECT_EQ(cfg.auth_type,         "api_key");   // default preserved
    EXPECT_EQ(cfg.max_script_size_kb, 512);        // default preserved
}

TEST_F(ConfigTest, InvalidJsonReturnsFalse) {
    fs::create_directories(tmp_dir / "reaclaw");
    std::ofstream f(tmp_dir / "reaclaw" / "config.json");
    f << "{not: valid json!!!";
    f.close();

    ReaClaw::Config cfg;
    EXPECT_FALSE(ReaClaw::Config::load(cfg, tmp_dir.string()));
}

TEST_F(ConfigTest, DerivedPathsAreSet) {
    ReaClaw::Config cfg;
    ASSERT_TRUE(ReaClaw::Config::load(cfg, tmp_dir.string()));

    EXPECT_FALSE(cfg.resource_dir.empty());
    EXPECT_FALSE(cfg.scripts_dir.empty());
    EXPECT_FALSE(cfg.certs_dir.empty());
    EXPECT_FALSE(cfg.resolved_cert_path.empty());
    EXPECT_FALSE(cfg.resolved_key_path.empty());

    // scripts_dir must live under resource_dir
    EXPECT_EQ(cfg.scripts_dir.rfind(cfg.resource_dir, 0), 0u);
}

TEST_F(ConfigTest, SaveAndReloadRoundTrip) {
    ReaClaw::Config cfg;
    ASSERT_TRUE(ReaClaw::Config::load(cfg, tmp_dir.string()));

    cfg.port     = 7777;
    cfg.auth_key = "round_trip_key";
    ASSERT_TRUE(cfg.save());

    ReaClaw::Config cfg2;
    ASSERT_TRUE(ReaClaw::Config::load(cfg2, tmp_dir.string()));
    EXPECT_EQ(cfg2.port,     7777);
    EXPECT_EQ(cfg2.auth_key, "round_trip_key");
}

TEST_F(ConfigTest, DirectoriesCreatedOnLoad) {
    ReaClaw::Config cfg;
    ASSERT_TRUE(ReaClaw::Config::load(cfg, tmp_dir.string()));

    EXPECT_TRUE(fs::is_directory(tmp_dir / "reaclaw"));
    EXPECT_TRUE(fs::is_directory(tmp_dir / "reaclaw" / "certs"));
    EXPECT_TRUE(fs::is_directory(tmp_dir / "reaclaw" / "scripts"));
}
