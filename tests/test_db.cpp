#include <gtest/gtest.h>
#include "db/db.h"
#include "util/logging.h"

#include <algorithm>

class DBTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        ReaClaw::Log::init(ReaClaw::LogLevel::error, "", nullptr);
    }

    ReaClaw::DB db;

    void SetUp() override {
        ASSERT_TRUE(db.open(":memory:"));
    }

    void TearDown() override {
        db.close();
    }
};

TEST_F(DBTest, OpenSucceeds) {
    EXPECT_TRUE(db.is_open());
}

TEST_F(DBTest, SchemaTablesExist) {
    auto rows = db.query(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", {});

    std::vector<std::string> names;
    for (auto& r : rows) names.push_back(r.at("name"));

    EXPECT_NE(std::find(names.begin(), names.end(), "actions"),           names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "scripts"),           names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "execution_history"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "meta"),              names.end());
}

TEST_F(DBTest, IndexesExist) {
    auto rows = db.query(
        "SELECT name FROM sqlite_master WHERE type='index' ORDER BY name", {});

    std::vector<std::string> names;
    for (auto& r : rows) names.push_back(r.at("name"));

    EXPECT_NE(std::find(names.begin(), names.end(), "idx_history_executed_at"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "idx_scripts_name"),        names.end());
}

TEST_F(DBTest, InsertAndQueryActions) {
    db.execute(
        "INSERT INTO actions(id, name, category, section) "
        "VALUES(40285, 'Track: Toggle mute', 'Track', 'main')");

    auto rows = db.query(
        "SELECT id, name, category FROM actions WHERE id = ?1", {"40285"});
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].at("name"),     "Track: Toggle mute");
    EXPECT_EQ(rows[0].at("category"), "Track");
}

TEST_F(DBTest, ScalarIntCount) {
    db.execute("INSERT INTO actions(id,name,category,section) VALUES(1,'A','C','main')");
    db.execute("INSERT INTO actions(id,name,category,section) VALUES(2,'B','C','main')");

    EXPECT_EQ(db.scalar_int("SELECT COUNT(*) FROM actions"), 2);
}

TEST_F(DBTest, ScalarIntDefaultOnMiss) {
    EXPECT_EQ(db.scalar_int("SELECT id FROM actions WHERE id=99999", {}, -1), -1);
}

TEST_F(DBTest, ScalarText) {
    db.execute("INSERT INTO meta(key,value) VALUES('version','0.1.0')");
    EXPECT_EQ(db.scalar_text("SELECT value FROM meta WHERE key='version'"), "0.1.0");
}

TEST_F(DBTest, ParameterizedQueryPreventsInjection) {
    db.execute("INSERT INTO actions(id,name,category,section) VALUES(1,'safe','C','main')");

    // Injection attempt must be treated as a literal string, not SQL
    auto rows = db.query("SELECT * FROM actions WHERE name = ?1",
                         {"' OR '1'='1"});
    EXPECT_TRUE(rows.empty());
}

TEST_F(DBTest, ExecutionHistoryInsertAndQuery) {
    db.query(
        "INSERT INTO execution_history(type, target_id, agent_id, status, error) "
        "VALUES(?1,?2,?3,?4,?5)",
        {"action", "40285", "sparky", "success", ""});

    auto rows = db.query(
        "SELECT type, target_id, agent_id, status FROM execution_history", {});
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].at("type"),      "action");
    EXPECT_EQ(rows[0].at("target_id"), "40285");
    EXPECT_EQ(rows[0].at("agent_id"),  "sparky");
    EXPECT_EQ(rows[0].at("status"),    "success");
}

TEST_F(DBTest, ScriptsInsertAndQuery) {
    db.query(
        "INSERT INTO scripts(id, name, body, script_path, tags) "
        "VALUES(?1,?2,?3,?4,?5)",
        {"_test_abc12345", "test_script",
         "reaper.ShowConsoleMsg('hi')", "/path/to/script.lua", "[]"});

    auto rows = db.query("SELECT id, name FROM scripts WHERE id = ?1",
                         {"_test_abc12345"});
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].at("name"), "test_script");
}

TEST_F(DBTest, CloseAndReopenIsClean) {
    db.execute("INSERT INTO actions(id,name,category,section) VALUES(1,'X','C','main')");
    db.close();
    EXPECT_FALSE(db.is_open());

    // Reopen in-memory — fresh schema, previous data gone
    ASSERT_TRUE(db.open(":memory:"));
    EXPECT_TRUE(db.is_open());
    EXPECT_EQ(db.scalar_int("SELECT COUNT(*) FROM actions"), 0);
}
