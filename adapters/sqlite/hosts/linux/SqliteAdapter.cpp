// The `sqlite` namespace on Windows (adapters/sqlite/spec.md) — durable local storage that survives
// independently of WebView-managed storage (contract §10). A reference adapter: request-driven,
// local, durable. Booleans bind as 0/1; blobs are out of the v1 text scope (§5a covers binary). Each
// open is a distinct connection closed on `close`, so the file handle is released (a later fs.remove
// on Windows would otherwise hit a sharing violation).
#include <cmath>
#include <unordered_map>

#include "adapters/Adapters.hpp"
#include "sqlite3.h"

namespace {

Json columnValue(sqlite3_stmt* stmt, int i) {
    switch (sqlite3_column_type(stmt, i)) {
        case SQLITE_INTEGER:
            return Json(static_cast<long long>(sqlite3_column_int64(stmt, i)));
        case SQLITE_FLOAT:
            return Json(sqlite3_column_double(stmt, i));
        case SQLITE_NULL:
            return Json(nullptr);
        case SQLITE_TEXT:
        case SQLITE_BLOB:
        default: {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            int n = sqlite3_column_bytes(stmt, i);
            return Json(std::string(txt ? txt : "", n));
        }
    }
}

void bindParam(sqlite3_stmt* stmt, int idx, const Json& v) {
    if (v.is_null()) sqlite3_bind_null(stmt, idx);
    else if (v.is_boolean()) sqlite3_bind_int64(stmt, idx, v.get<bool>() ? 1 : 0);  // SQLite has no bool
    else if (v.is_string()) {
        std::string s = v.get<std::string>();
        sqlite3_bind_text(stmt, idx, s.c_str(), (int)s.size(), SQLITE_TRANSIENT);
    } else if (v.is_number_integer() || v.is_number_unsigned()) {
        sqlite3_bind_int64(stmt, idx, v.get<long long>());
    } else if (v.is_number_float()) {
        double d = v.get<double>();
        if (d == std::floor(d) && std::isfinite(d) && std::fabs(d) < 9.2e18)
            sqlite3_bind_int64(stmt, idx, static_cast<long long>(d));
        else
            sqlite3_bind_double(stmt, idx, d);
    } else {
        std::string s = v.dump();
        sqlite3_bind_text(stmt, idx, s.c_str(), (int)s.size(), SQLITE_TRANSIENT);
    }
}

class SqliteAdapter : public IAdapter {
public:
    ~SqliteAdapter() override {
        for (auto& [h, db] : dbs_) sqlite3_close(db);
    }

    std::string ns() const override { return "sqlite"; }

    Json handle(const std::string& command, const Json& args) override {
        if (command == "open") return open(ja::reqString(args, "path"));
        if (command == "execute") return execute(args);
        if (command == "query") return query(args);
        if (command == "close") return close(args);
        throw EngawaError::nosys("sqlite." + command);
    }

private:
    Json open(const std::string& path) {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "cannot open database";
            if (db) sqlite3_close(db);
            throw EngawaError("ESQLITE", msg);
        }
        int handle = next_++;
        dbs_[handle] = db;
        return Json{{"db", handle}};
    }

    // Prepare `sql` on the connection and bind the positional `?` params (spec: params is a positional
    // array — SQLite binds `?` by 1-based index natively, so no placeholder rewriting is needed).
    sqlite3_stmt* prepare(sqlite3* db, const Json& args) {
        std::string sql = ja::reqString(args, "sql");
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            throw EngawaError("ESQLITE", sqlite3_errmsg(db));
        if (args.is_object() && args.contains("params") && args["params"].is_array()) {
            const Json& p = args["params"];
            for (int i = 0; i < (int)p.size(); i++) bindParam(stmt, i + 1, p[i]);
        }
        return stmt;
    }

    Json execute(const Json& args) {
        sqlite3* db = handle(args);
        sqlite3_stmt* stmt = prepare(db, args);
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) { /* drain any rows */ }
        if (rc != SQLITE_DONE) {
            std::string msg = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            throw EngawaError("ESQLITE", msg);
        }
        sqlite3_finalize(stmt);
        return Json{{"changes", sqlite3_changes(db)},
                    {"lastInsertRowid", static_cast<long long>(sqlite3_last_insert_rowid(db))}};
    }

    Json query(const Json& args) {
        sqlite3* db = handle(args);
        sqlite3_stmt* stmt = prepare(db, args);
        Json rows = Json::array();
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            Json row = Json::object();
            int cols = sqlite3_column_count(stmt);
            for (int i = 0; i < cols; i++)
                row[sqlite3_column_name(stmt, i)] = columnValue(stmt, i);
            rows.push_back(std::move(row));
        }
        if (rc != SQLITE_DONE) {
            std::string msg = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            throw EngawaError("ESQLITE", msg);
        }
        sqlite3_finalize(stmt);
        return Json{{"rows", rows}};
    }

    Json close(const Json& args) {
        int h = ja::reqInt(args, "db");
        auto it = dbs_.find(h);
        if (it == dbs_.end()) throw EngawaError("EBADF", "unknown db handle");
        sqlite3_close(it->second);
        dbs_.erase(it);
        return Json(nullptr);
    }

    sqlite3* handle(const Json& args) {
        int h = ja::reqInt(args, "db");
        auto it = dbs_.find(h);
        if (it != dbs_.end()) return it->second;
        throw EngawaError("EBADF", "unknown db handle");
    }

    std::unordered_map<int, sqlite3*> dbs_;
    int next_ = 1;
};

}  // namespace

std::unique_ptr<IAdapter> makeSqliteAdapter() { return std::make_unique<SqliteAdapter>(); }
