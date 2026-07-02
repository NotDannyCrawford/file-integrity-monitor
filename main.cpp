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

// Turn the 64-bit hash into a 16-char hex string
std::string toHex(uint64_t value) {
    std::stringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
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
    std::cout << "Database ready. Scanning: " << folder << std::endl;

    // NEW: prepare the INSERT once, with ? placeholders
    const char* insertSql =
        "INSERT OR REPLACE INTO files (filename, hash, size) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Prepare failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return 1;
    }

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::string hash = toHex(hashFile(entry.path()));
            long long   size = entry.file_size();

            // NEW: fill the ? placeholders (they're numbered from 1)
            sqlite3_bind_text (stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text (stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, size);

            // NEW: run it, then reset so we can reuse it for the next file
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "Insert failed: " << sqlite3_errmsg(db) << std::endl;
            }
            sqlite3_reset(stmt);

            std::cout << "  stored " << name << "  (" << size << " bytes, " << hash << ")" << std::endl;
        }
    }

    sqlite3_finalize(stmt);   // NEW: release the prepared statement
    sqlite3_close(db);
    return 0;
}