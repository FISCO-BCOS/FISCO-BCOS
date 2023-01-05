#include "bcos-task/SequenceScheduler.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/ConcurrentStorage.h>
#include <bcos-task/TBBScheduler.h>
#include <bcos-task/Wait.h>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <boost/lexical_cast.hpp>
#include <chrono>
#include <iostream>
#include <random>
#include <range/v3/view/transform.hpp>

using namespace bcos;

template <>
struct std::hash<std::tuple<std::string, std::string>>
{
    auto operator()(std::tuple<std::string, std::string> const& str) const noexcept
    {
        auto hash = std::hash<std::string>{}(std::get<0>(str));
        return hash;
    }
};

// Total 64 tables
using TableKey = std::tuple<std::string, std::string>;
constexpr static size_t tableCount = 64;

std::vector<std::tuple<TableKey, storage::Entry>> generatRandomData(int count)
{
    std::cout << "Generating random data..." << std::endl;
    std::vector<std::tuple<TableKey, storage::Entry>> dataSet(count);

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

void testStorage2BatchWrite(auto& storage, RANGES::range auto const& dataSet)
{
    task::syncWait([](decltype(storage)& storage, decltype(dataSet)& dataSet) -> task::Task<void> {
        auto now = std::chrono::steady_clock::now();

        co_await storage.write(
            dataSet | RANGES::views::transform([](auto& item) { return std::get<0>(item); }),
            dataSet | RANGES::views::transform(
                          [](auto& item) -> auto const& { return std::get<1>(item); }));

        auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - now)
                           .count();

        std::cout << "Storage2 batchWrite elpased: " << elpased << "ms" << std::endl;
        co_return;
    }(storage, dataSet));
}

void testStorage2SingleWrite(auto& storage, RANGES::range auto const& dataSet)
{
    task::syncWait([](decltype(storage)& storage, decltype(dataSet)& dataSet) -> task::Task<void> {
        auto now = std::chrono::steady_clock::now();
        for (const auto& item : dataSet)
        {
            co_await storage.write(
                storage2::single(std::get<0>(item)), storage2::single(std::get<1>(item)));
        }
        auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - now)
                           .count();
        std::cout << "Storage2 singleWrite elpased: " << elpased << "ms" << std::endl;
        co_return;
    }(storage, dataSet));
}

void testStorage2BatchRead(auto& storage, RANGES::range auto const& keySet)
{
    task::syncWait([](decltype(storage)& storage, decltype(keySet)& keySet) -> task::Task<void> {
        auto now = std::chrono::steady_clock::now();

        auto it = co_await storage.read(keySet);
        while (co_await it.next())
        {
            assert(co_await it.hasValue());
            [[maybe_unused]] auto key = co_await it.key();
            [[maybe_unused]] auto value = co_await it.value();

            // some operator of key and value
        }
        auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - now)
                           .count();
        std::cout << "Storage2 batchRead elpased: " << elpased << "ms" << std::endl;
        co_return;
    }(storage, keySet));
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " type[0-batch,1-single] count" << std::endl;
        return 1;
    }

    auto type = boost::lexical_cast<int>(argv[1]);
    auto count = boost::lexical_cast<int>(argv[2]);
    auto dataSet = generatRandomData(count);

    storage2::concurrent_storage::ConcurrentStorage<TableKey, storage::Entry, true, true, false>
        storage;
    if (type == 0)
    {
        testStorage2BatchWrite(storage, dataSet);
        testStorage2BatchRead(storage, dataSet | RANGES::views::transform([
        ](auto& item) -> auto& { return std::get<0>(item); }));
    }
    else
    {
        testStorage2SingleWrite(storage, dataSet);
    }
}