#include <gtest/gtest.h>
#include "util/logging.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

class LoggingTest : public ::testing::Test {
protected:
    fs::path log_file;

    void SetUp() override {
        log_file = fs::temp_directory_path() /
                   ("reaclaw_log_" + std::to_string(
                        std::chrono::system_clock::now().time_since_epoch().count()) + ".log");
        fs::remove(log_file);
    }

    void TearDown() override {
        // Detach from file so the next test can open a fresh one
        ReaClaw::Log::init(ReaClaw::LogLevel::error, "", nullptr);
        fs::remove(log_file);
    }

    std::string read_log() {
        std::ifstream f(log_file);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
};

TEST_F(LoggingTest, TextFormatContainsLevelAndMessage) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr);
    ReaClaw::Log::info("hello world");

    std::string out = read_log();
    EXPECT_NE(out.find("INFO"),        std::string::npos);
    EXPECT_NE(out.find("hello world"), std::string::npos);
}

TEST_F(LoggingTest, TextFormatHasReaClawPrefix) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr);
    ReaClaw::Log::info("prefix test");

    EXPECT_EQ(read_log().rfind("ReaClaw", 0), 0u);
}

TEST_F(LoggingTest, JsonFormatStartsWithBrace) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr, "json");
    ReaClaw::Log::info("json test");

    std::string out = read_log();
    EXPECT_EQ(out[0], '{');
}

TEST_F(LoggingTest, JsonFormatContainsAllFields) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr, "json");
    ReaClaw::Log::warn("check fields");

    std::string out = read_log();
    EXPECT_NE(out.find("\"level\""),       std::string::npos);
    EXPECT_NE(out.find("\"ts\""),          std::string::npos);
    EXPECT_NE(out.find("\"msg\""),         std::string::npos);
    EXPECT_NE(out.find("check fields"),    std::string::npos);
}

TEST_F(LoggingTest, JsonLevelNamesAreLowercase) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr, "json");
    ReaClaw::Log::debug("d");
    ReaClaw::Log::info("i");
    ReaClaw::Log::warn("w");
    ReaClaw::Log::error("e");

    std::string out = read_log();
    EXPECT_NE(out.find("\"debug\""), std::string::npos);
    EXPECT_NE(out.find("\"info\""),  std::string::npos);
    EXPECT_NE(out.find("\"warn\""),  std::string::npos);
    EXPECT_NE(out.find("\"error\""), std::string::npos);
}

TEST_F(LoggingTest, LevelFilterSuppressesLower) {
    ReaClaw::Log::init(ReaClaw::LogLevel::warn, log_file.string(), nullptr);
    ReaClaw::Log::debug("debug msg");
    ReaClaw::Log::info("info msg");
    ReaClaw::Log::warn("warn msg");
    ReaClaw::Log::error("error msg");

    std::string out = read_log();
    EXPECT_EQ(out.find("debug msg"), std::string::npos);
    EXPECT_EQ(out.find("info msg"),  std::string::npos);
    EXPECT_NE(out.find("warn msg"),  std::string::npos);
    EXPECT_NE(out.find("error msg"), std::string::npos);
}

TEST_F(LoggingTest, JsonEscapesDoubleQuotes) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr, "json");
    ReaClaw::Log::info(R"(has "quotes")");

    EXPECT_NE(read_log().find(R"(\")"), std::string::npos);
}

TEST_F(LoggingTest, JsonEscapesNewlines) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr, "json");
    ReaClaw::Log::info("line1\nline2");

    std::string out = read_log();
    EXPECT_NE(out.find("\\n"), std::string::npos);
    // The JSON line itself should be a single line
    EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 1);
}

TEST_F(LoggingTest, ReinitWithNewFileWorks) {
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file.string(), nullptr);
    ReaClaw::Log::info("first file");

    fs::path log_file2 = log_file.string() + ".2";
    ReaClaw::Log::init(ReaClaw::LogLevel::debug, log_file2.string(), nullptr);
    ReaClaw::Log::info("second file");
    ReaClaw::Log::init(ReaClaw::LogLevel::error, "", nullptr);  // detach

    // First file should have first message, second file should have second
    EXPECT_NE(read_log().find("first file"), std::string::npos);
    {
        std::ifstream f2(log_file2);
        std::string out2(std::istreambuf_iterator<char>(f2), {});
        EXPECT_NE(out2.find("second file"), std::string::npos);
        EXPECT_EQ(out2.find("first file"), std::string::npos);
    }
    fs::remove(log_file2);
}
