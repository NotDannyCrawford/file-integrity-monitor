#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>    // for reading file contents
#include <cstdint>    // for uint64_t

namespace fs = std::filesystem;

// Compute a 64-bit FNV-1a hash of a file's bytes.
uint64_t hashFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);   // open the file in binary mode
    uint64_t hash  = 14695981039346656037ULL;     // FNV offset basis (the starting value)
    const uint64_t prime = 1099511628211ULL;       // FNV prime

    char byte;
    while (file.get(byte)) {                        // read one byte at a time
        hash ^= static_cast<unsigned char>(byte);   // XOR the byte in
        hash *= prime;                              // multiply by the prime
    }
    return hash;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <folder_path>" << std::endl;
        return 1;
    }

    std::string folder = argv[1];
    std::cout << "Scanning: " << folder << std::endl;

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            uint64_t hash = hashFile(entry.path());
            std::cout << entry.path().filename().string()
                      << "  (" << entry.file_size() << " bytes)"
                      << "  hash: " << std::hex << hash << std::dec
                      << std::endl;
        }
    }
    return 0;
}