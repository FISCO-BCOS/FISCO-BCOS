// Minimal RocksDB reader for the B3 node — reads an arbitrary `table:key` and prints the raw
// value as hex. Keys that are raw binary (e.g. s_hash_2_number keys are the 32-byte block hash)
// are passed as `hex:<hex-encoded-key>` so callers can round-trip a value read back into a key.
// Used by state_verify.py for the block-table cross-checks (spec B.1/B.2).
// Build: c++ -O2 op_state_read.cpp -o op_state_read -std=c++17 -lrocksdb
#include <rocksdb/db.h>
#include <iostream>
#include <string>
int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: op_state_read <rocksdb_dir> <table:key | hex:<hex-key>>\n";
        return 2;
    }
    rocksdb::Options opts;
    opts.create_if_missing = false;
    std::unique_ptr<rocksdb::DB> db;
    auto s = rocksdb::DB::OpenForReadOnly(opts, argv[1], &db);
    if (!s.ok())
    {
        std::cerr << "open failed: " << s.ToString() << "\n";
        return 1;
    }
    std::string key = argv[2];
    std::string dbKey;
    // Mixed form `table:hex:<hex-key-bytes>`: the table prefix stays a literal string and the
    // hex part decodes to the raw key bytes (s_hash_2_number keys are the 32-byte block hash).
    auto hexPos = key.find(":hex:");
    if (hexPos != std::string::npos)
    {
        dbKey = key.substr(0, hexPos);  // includes trailing ':'
        auto hexPart = key.substr(hexPos + 5);
        for (size_t i = 0; i + 1 < hexPart.size(); i += 2)
        {
            unsigned b;
            sscanf(hexPart.c_str() + i, "%2x", &b);
            dbKey.push_back(static_cast<char>(b));
        }
    }
    else if (key.rfind("hex:", 0) == 0)
    {
        auto hexPart = key.substr(4);
        for (size_t i = 0; i + 1 < hexPart.size(); i += 2)
        {
            unsigned b;
            sscanf(hexPart.c_str() + i, "%2x", &b);
            dbKey.push_back(static_cast<char>(b));
        }
    }
    else
    {
        dbKey = key;
    }
    std::string val;
    auto st = db->Get(rocksdb::ReadOptions(), dbKey, &val);
    if (!st.ok())
    {
        std::cout << "NOT_FOUND\n";
        return 0;
    }
    std::cout << "hex=";
    for (unsigned char c : val)
        printf("%02x", c);
    std::cout << "\n";
    return 0;
}
