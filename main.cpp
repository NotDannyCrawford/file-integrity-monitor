#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <set>
#include <vector>
#include <sqlite3.h>

namespace fs = std::filesystem;

// FNV-1a constants, published in the algorithm's spec.
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime       = 1099511628211ULL;
constexpr int      kHashHexWidth   = 16;   // 64 bits = 16 hex digits

// Compute a 64-bit FNV-1a hash of a file's bytes.
// Sets ok to false (and returns 0) if the file can't be opened.
uint64_t hashFile(const fs::path& path, bool& ok) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ok = false;
        return 0;
    }
    ok = true;
    uint64_t hash = kFnvOffsetBasis;
    char byte;
    while (file.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= kFnvPrime;
    }
    return hash;
}

// Turn the 64-bit hash into a fixed-width hex string.
std::string toHex(uint64_t value) {
    std::stringstream ss;
    ss << std::hex << std::setw(kHashHexWidth) << std::setfill('0') << value;
    return ss.str();
}

// Look up a file's stored hash. Returns "" if it's not in the DB yet.
std::string getStoredHash(sqlite3* db, const std::string& filename) {
    const char* sql = "SELECT hash FROM files WHERE filename = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Lookup failed: " << sqlite3_errmsg(db) << std::endl;
        return "";
    }
    sqlite3_bind_text(stmt, 1, filename.c_str(), -1, SQLITE_TRANSIENT);

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {           // a matching row exists
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) result = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    return result;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <folder_path>" << std::endl;
        return 1;
    }
    std::string folder = argv[1];
    if (!fs::is_directory(folder)) {
        std::cerr << "Not a folder: " << folder << std::endl;
        return 1;
    }

    sqlite3* db;
    if (sqlite3_open("baseline.db", &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    const char* createSql =
        "CREATE TABLE IF NOT EXISTS files ("
        "  filename  TEXT PRIMARY KEY,"
        "  hash      TEXT,"
        "  size      INTEGER,"
        "  last_seen TEXT DEFAULT CURRENT_TIMESTAMP"
        ");";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, createSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "Create table failed: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return 1;
    }
    std::cout << "Scanning: " << folder << std::endl;

    const char* insertSql =
        "INSERT OR REPLACE INTO files (filename, hash, size) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return 1;
    }

    std::set<std::string> seen;   // track which files exist on disk this scan

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            seen.insert(name);

            bool readable = false;
            uint64_t rawHash = hashFile(entry.path(), readable);
            if (!readable) {
                // report it, keep its old baseline row, move on
                std::cerr << "  [SKIPPED]  " << name << " (can't read)" << std::endl;
                continue;
            }
            std::string hash = toHex(rawHash);
            long long   size = entry.file_size();

            // compare against the stored baseline
            std::string stored = getStoredHash(db, name);
            if (stored.empty()) {
                std::cout << "  [NEW]      " << name << std::endl;
            } else if (stored != hash) {
                std::cout << "  [MODIFIED] " << name << std::endl;
            } else {
                std::cout << "  [OK]       " << name << std::endl;
            }

            // update the baseline with the current state
            sqlite3_bind_text (stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text (stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, size);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
    }
    sqlite3_finalize(stmt);

    // Detect DELETED files: in the baseline, but not seen on disk this scan.
    const char* selectAll = "SELECT filename FROM files;";
    sqlite3_stmt* selStmt = nullptr;
    if (sqlite3_prepare_v2(db, selectAll, -1, &selStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Select failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return 1;
    }

    std::vector<std::string> deleted;
    while (sqlite3_step(selStmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(selStmt, 0);
        if (!text) continue;
        std::string dbName = reinterpret_cast<const char*>(text);
        if (seen.find(dbName) == seen.end()) {        // in DB, gone from disk
            std::cout << "  [DELETED]  " << dbName << std::endl;
            deleted.push_back(dbName);
        }
    }
    sqlite3_finalize(selStmt);

    // Remove the deleted files from the baseline so it reflects current reality.
    const char* delSql = "DELETE FROM files WHERE filename = ?;";
    sqlite3_stmt* delStmt = nullptr;
    if (sqlite3_prepare_v2(db, delSql, -1, &delStmt, nullptr) != SQLITE_OK) {
        std::cerr << "Delete prepare failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return 1;
    }
    for (const auto& name : deleted) {
        sqlite3_bind_text(delStmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(delStmt);
        sqlite3_reset(delStmt);
    }
    sqlite3_finalize(delStmt);

    sqlite3_close(db);
    return 0;
}
