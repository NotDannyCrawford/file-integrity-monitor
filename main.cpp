#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <sqlite3.h>

namespace fs = std::filesystem;

uint64_t hashFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    char byte;
    while (file.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= prime;
    }
    return hash;
}

std::string toHex(uint64_t value) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
}

// NEW: look up a file's stored hash. Returns "" if it's not in the DB yet.
std::string getStoredHash(sqlite3* db, const std::string& filename) {
    const char* sql = "SELECT hash FROM files WHERE filename = ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
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
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <folder_path>" << std::endl;
        return 1;
    }
    std::string folder = argv[1];

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
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr);

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::string hash = toHex(hashFile(entry.path()));
            long long   size = entry.file_size();

            // NEW: compare against what we stored last time
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
    sqlite3_close(db);
    return 0;
}