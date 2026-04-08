#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace ReaClaw {

using Row = std::unordered_map<std::string, std::string>;
using Rows = std::vector<Row>;

class DB {
   public:
    DB() = default;
    ~DB();

    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    // Open or create the database and run schema migrations.
    bool open(const std::string& path);
    void close();
    bool is_open() const {
        return db_ != nullptr;
    }

    // Execute SQL with no result rows (DDL, INSERT, UPDATE, DELETE).
    bool execute(const std::string& sql);

    // Parameterized query with text bindings (?1, ?2, ...).
    Rows query(const std::string& sql, const std::vector<std::string>& params = {});

    // Parameterized query with integer bindings.
    Rows query_i(const std::string& sql, const std::vector<int64_t>& params = {});

    // Convenience: single integer scalar (or default_val on empty result).
    int64_t scalar_int(const std::string& sql,
                       const std::vector<std::string>& params = {},
                       int64_t default_val = 0);

    // Convenience: single text scalar (or default_val on empty result).
    std::string scalar_text(const std::string& sql,
                            const std::vector<std::string>& params = {},
                            const std::string& default_val = "");

    int64_t last_insert_rowid() const;

   private:
    sqlite3* db_ = nullptr;
    bool run_schema();
};

}  // namespace ReaClaw
