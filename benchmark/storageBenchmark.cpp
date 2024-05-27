#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Wait.h"
#include <tbb/blocked_range.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <boost/lexical_cast.hpp>
#include <chrono>
#include <iostream>
#include <random>

using namespace bcos;
using namespace bcos::storage2::memory_storage;

using Key = std::tuple<std::string, std::string>;

template <>
struct std::hash<Key>
{
    auto operator()(Key const& str) const noexcept
    {
        auto hash = std::hash<std::string>{}(std::get<0>(str));
        boost::hash_combine(hash, std::hash<std::string>{}(std::get<1>(str)));
        return hash;
    }
};

struct TBBCompare
{
    static auto hash(Key const& str)
    {
        return std::hash<std::tuple<std::string, std::string>>{}(str);
    }

    static auto equal(Key const& lhs, Key const& rhs) noexcept { return lhs == rhs; }
};

// Total 64 tables
constexpr static size_t tableCount = 64;

using TBBHashMap = tbb::concurrent_hash_map<Key, storage::Entry, TBBCompare>;
using TBBUnorderedMap = tbb::concurrent_unordered_map<Key, storage::Entry, std::hash<Key>>;

std::vector<std::tuple<Key, storage::Entry>> generatRandomData(int count)
{
    std::cout << "Generating random data..." << std::endl;
    std::vector<std::tuple<Key, storage::Entry>> dataSet(count);

    tbb::parallel_for(tbb::blocked_range(0, count), [&dataSet](auto const& range) {
        std::mt19937 random(std::random_device{}());
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto& [tableKey, entry] = dataSet[i];
            auto& [table, key] = tableKey;
            table = "table:" + boost::lexical_cast<std::string>(i % tableCount);
            key = "key:" + boost::lexical_cast<std::string>(random());
            entry.set(boost::lexical_cast<std::string>(random()));
        }
    });

    std::cout << "Generate random finished" << std::endl;
    return dataSet;
}

task::Task<void> storage2BatchWrite(auto& storage, RANGES::range auto const& dataSet)
{
    auto now = std::chrono::steady_clock::now();

    co_await storage2::writeSome(storage, dataSet | RANGES::views::transform([
    ](auto& item) -> auto const& { return std::get<0>(item); }),
        dataSet |
            RANGES::views::transform([](auto& item) -> auto const& { return std::get<1>(item); }));

    auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - now)
                       .count();

    std::cout << "Storage2 batch write elpased: " << elpased << "ms" << std::endl;
    co_return;
}

task::Task<void> storage2MultiThreadWrite(auto& storage, RANGES::range auto const& dataSet)
{
    auto now = std::chrono::steady_clock::now();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, RANGES::size(dataSet)), [&storage, &dataSet](
                                                                                const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto const& item = dataSet[i];
            task::tbb::syncWait(storage2::writeOne(storage, std::get<0>(item), std::get<1>(item)));
        }
    });
    auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - now)
                       .count();
    std::cout << "Storage2 multi thread write elpased: " << elpased << "ms" << std::endl;
    co_return;
}

void tbbBatchWrite(auto& storage, RANGES::range auto const& dataSet)
{
    auto now = std::chrono::steady_clock::now();
    for (auto const& [key, value] : dataSet)
    {
        storage.emplace(key, value);
    }
    auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - now)
                       .count();
    std::cout << "TBBHashMap batch write elpased: " << elpased << "ms" << std::endl;
}

void tbbMultiThreadWrite(auto& storage, RANGES::range auto const& dataSet)
{
    auto now = std::chrono::steady_clock::now();
    tbb::parallel_for(tbb::blocked_range<size_t>(0, RANGES::size(dataSet)),
        [&storage, &dataSet](const auto& range) {
            for (auto i = range.begin(); i != range.end(); ++i)
            {
                auto const& [key, value] = dataSet[i];
                storage.emplace(key, value);
            }
        });

    auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - now)
                       .count();
    std::cout << "TBBHashMap multi thread write elpased: " << elpased << "ms" << std::endl;
}

int main(int argc, char* argv[])
{
    return task::syncWait([](int argc, char* argv[]) -> task::Task<int> {
        if (argc < 2)
        {
            std::cout << "Usage: " << argv[0] << " count" << std::endl;
            co_return 1;
        }

        auto count = boost::lexical_cast<int>(argv[1]);
        auto dataSet = generatRandomData(count);

        std::cout << std::endl << "Testing storage2 memory storage..." << std::endl;
        storage2::memory_storage::MemoryStorage<Key, storage::Entry,
            Attribute(ORDERED | CONCURRENT), std::hash<Key>>
            newStorage1;
        co_await storage2BatchWrite(newStorage1, dataSet);

        storage2::memory_storage::MemoryStorage<Key, storage::Entry,
            Attribute(ORDERED | CONCURRENT), std::hash<Key>>
            newStorage2;
        co_await storage2MultiThreadWrite(newStorage2, dataSet);

        std::cout << std::endl << "Testing TBB hash map..." << std::endl;
        TBBHashMap tbbHashMap;
        tbbBatchWrite(tbbHashMap, dataSet);

        TBBHashMap tbbHashMap2;
        tbbMultiThreadWrite(tbbHashMap2, dataSet);

        std::cout << std::endl << "Testing TBB unordered map..." << std::endl;
        TBBUnorderedMap tbbUnorderedMap;
        tbbBatchWrite(tbbUnorderedMap, dataSet);

        TBBUnorderedMap tbbUnorderedMap2;
        tbbMultiThreadWrite(tbbUnorderedMap2, dataSet);

        co_return 0;
    }(argc, argv));
}