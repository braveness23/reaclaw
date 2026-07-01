#include "db/db.h"

#include "util/logging.h"

#include <sqlite3.h>

#include <string>

namespace ReaClaw {

namespace {

static const char k_schema[] = R"SQL(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS actions (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    category   TEXT NOT NULL DEFAULT 'General',
    section    TEXT NOT NULL DEFAULT 'main',
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE VIRTUAL TABLE IF NOT EXISTS actions_fts
    USING fts5(name, category, content=actions, content_rowid=id);

-- MIDI editor section actions are kept in their own table because their command
-- IDs share the numeric namespace of the main section (a single `id` primary key
-- across both would collide). Queried via /catalog?section=midi_editor.
CREATE TABLE IF NOT EXISTS actions_midi (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    category   TEXT NOT NULL DEFAULT 'General',
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE VIRTUAL TABLE IF NOT EXISTS actions_midi_fts
    USING fts5(name, category, content=actions_midi, content_rowid=id);

CREATE TABLE IF NOT EXISTS scripts (
    id              TEXT PRIMARY KEY,
    name            TEXT NOT NULL,
    body            TEXT NOT NULL,
    script_path     TEXT NOT NULL,
    tags            TEXT NOT NULL DEFAULT '[]',
    execution_count INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    last_executed   TEXT
);

CREATE TABLE IF NOT EXISTS execution_history (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    type        TEXT NOT NULL,
    target_id   TEXT NOT NULL,
    agent_id    TEXT,
    status      TEXT NOT NULL,
    error       TEXT,
    executed_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_history_executed_at
    ON execution_history(executed_at DESC);

CREATE INDEX IF NOT EXISTS idx_scripts_name
    ON scripts(name);

-- Epic #20 prep: stored project-state snapshots for the shared snapshot/diff
-- layer (also backs the #19 A/B diff).
CREATE TABLE IF NOT EXISTS state_snapshots (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    taken_at  TEXT NOT NULL DEFAULT (datetime('now')),
    label     TEXT NOT NULL DEFAULT '',
    json      TEXT NOT NULL
);

-- Epic #20: local, opt-in learning. Each structured edit is an event; observed
-- antecedent->consequent transitions (within a window, same agent) accumulate in
-- learn_pairs. Mined into "after X, agents usually do Y" suggestions. Local only.
CREATE TABLE IF NOT EXISTS learn_events (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    ts       TEXT NOT NULL DEFAULT (datetime('now')),
    agent_id TEXT NOT NULL DEFAULT '',
    action   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_learn_events_agent
    ON learn_events(agent_id, id DESC);

CREATE TABLE IF NOT EXISTS learn_pairs (
    antecedent TEXT NOT NULL,
    consequent TEXT NOT NULL,
    n          INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (antecedent, consequent)
);

-- Issue #10 — server-side semantic catalog search (opt-in, off by default;
-- see config.semantic_search_enabled). Caches L2-normalized embedding vectors
-- (so cosine similarity is a plain dot product) as comma-separated decimal
-- text rather than a BLOB, to avoid adding blob-param binding to the DB
-- wrapper for a single caller. Built lazily on first semantic search, keyed
-- by (catalog snapshot, model) via meta.action_embeddings_sig so a catalog
-- rebuild or model change invalidates the cache automatically.
CREATE TABLE IF NOT EXISTS action_embeddings (
    id      INTEGER NOT NULL,
    section TEXT NOT NULL DEFAULT 'main',
    model   TEXT NOT NULL,
    vector  TEXT NOT NULL,
    PRIMARY KEY (id, section, model)
);
)SQL";

}  // namespace

DB::~DB() {
    close();
}

bool DB::open(const std::string& path) {
    if (db_)
        close();

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        Log::error("DB open failed (" + path + "): " + std::string(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA synchronous=NORMAL");
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=5000");

    return run_schema();
}

void DB::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool DB::execute(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        Log::error("DB execute failed: " + msg);
        return false;
    }
    return true;
}

Rows DB::query(const std::string& sql, const std::vector<std::string>& params) {
    Rows rows;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Log::error("DB prepare failed: " + std::string(sqlite3_errmsg(db_)) +
                   " | SQL: " + sql.substr(0, 120));
        return rows;
    }
    for (int i = 0; i < (int)params.size(); i++) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    int ncols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row row;
        for (int c = 0; c < ncols; c++) {
            const char* col_name = sqlite3_column_name(stmt, c);
            const unsigned char* val = sqlite3_column_text(stmt, c);
            row[col_name ? col_name : ""] = val ? reinterpret_cast<const char*>(val) : "";
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return rows;
}

Rows DB::query_i(const std::string& sql, const std::vector<int64_t>& params) {
    Rows rows;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Log::error("DB prepare_i failed: " + std::string(sqlite3_errmsg(db_)));
        return rows;
    }
    for (int i = 0; i < (int)params.size(); i++) {
        sqlite3_bind_int64(stmt, i + 1, params[i]);
    }
    int ncols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row row;
        for (int c = 0; c < ncols; c++) {
            const char* col_name = sqlite3_column_name(stmt, c);
            const unsigned char* val = sqlite3_column_text(stmt, c);
            row[col_name ? col_name : ""] = val ? reinterpret_cast<const char*>(val) : "";
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return rows;
}

int64_t DB::last_insert_rowid() const {
    return sqlite3_last_insert_rowid(db_);
}

int64_t DB::scalar_int(const std::string& sql,
                       const std::vector<std::string>& params,
                       int64_t default_val) {
    auto rows = query(sql, params);
    if (rows.empty() || rows[0].empty())
        return default_val;
    try {
        return std::stoll(rows[0].begin()->second);
    } catch (...) {
    }
    return default_val;
}

std::string DB::scalar_text(const std::string& sql,
                            const std::vector<std::string>& params,
                            const std::string& default_val) {
    auto rows = query(sql, params);
    if (rows.empty() || rows[0].empty())
        return default_val;
    return rows[0].begin()->second;
}

bool DB::run_schema() {
    if (!execute(k_schema))
        return false;
    // Migration: add reaper_cmd_id column if it doesn't exist (added in v0.3).
    // SQLite has no ADD COLUMN IF NOT EXISTS, so we check pragma_table_info first.
    auto cols = query("SELECT name FROM pragma_table_info('scripts') WHERE name='reaper_cmd_id'",
                      {});
    if (cols.empty()) {
        execute("ALTER TABLE scripts ADD COLUMN reaper_cmd_id INTEGER NOT NULL DEFAULT 0");
    }
    // Migration: add target_name to execution_history (added in v1.1) so the
    // audit log is readable, not just numeric ids.
    auto hcols = query(
            "SELECT name FROM pragma_table_info('execution_history') WHERE name='target_name'", {});
    if (hcols.empty()) {
        execute("ALTER TABLE execution_history ADD COLUMN target_name TEXT");
    }
    return true;
}

}  // namespace ReaClaw
