// List keys under a table prefix in the B3 RocksDB (first N), hex-encoded, for diagnosing
// physical key formats (spec B.1 table cross-checks).
// Build: c++ -O2 op_state_list.cpp -o op_state_list -std=c++17 -lrocksdb
#include <rocksdb/db.h>
#include <iostream>
#include <string>
int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: op_state_list <rocksdb_dir> <table-prefix> [max]\n";
        return 2;
    }
    rocksdb::Options opts;
    opts.create_if_missing = false;
    std::unique_ptr<rocksdb::DB> db;
    auto s = rocksdb::DB::OpenForReadOnly(opts, argv[1], &db);
    if (!s.ok())
    {
        std::cerr << "open failed\n";
        return 1;
    }
    int max = argc > 3 ? atoi(argv[3]) : 5;
    std::string prefix = argv[2];
    rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
    int n = 0;
    for (it->Seek(prefix); it->Valid() && n < max; it->Next())
    {
        auto k = it->key().ToString();
        if (k.rfind(prefix, 0) != 0)
            break;
        std::cout << "key hex=";
        for (unsigned char c : k)
            printf("%02x", c);
        std::cout << " ascii=\"" << k << "\"";
        std::cout << "\n";
        ++n;
    }
    delete it;
    return 0;
}
