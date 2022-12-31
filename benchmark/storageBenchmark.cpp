#include "bcos-task/SequenceScheduler.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/ConcurrentOrderedStorage.h>
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

std::vector<std::tuple<std::string, storage::Entry>> generatRandomData(int count)
{
    std::cout << "Generating random data..." << std::endl;
    std::vector<std::tuple<std::string, storage::Entry>> dataSet(count);

    tbb::parallel_for(tbb::blocked_range(0, count), [&dataSet](auto const& range) {
        std::mt19937 random(std::random_device{}());
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto& [key, entry] = dataSet[i];
            key = "key:" + boost::lexical_cast<std::string>(random());
            entry.set(boost::lexical_cast<std::string>(random()));
        }
    });

    std::cout << "Generate random finished" << std::endl;
    return dataSet;
}

void testStorage2BatchWrite(std::vector<std::tuple<std::string, storage::Entry>> const& dataSet)
{
    task::syncWait([&dataSet]() -> task::Task<void> {
        storage2::concurrent_ordered_storage::ConcurrentOrderedStorage<false,
            std::tuple<std::string, std::string>, storage::Entry>
            storage;

        auto now = std::chrono::steady_clock::now();

        co_await storage.write(dataSet | RANGES::views::transform([](auto& item) {
            return std::tuple<std::string, std::string>(std::string("table"), std::get<0>(item));
        }),
            dataSet | RANGES::views::transform(
                          [](auto& item) -> auto const& { return std::get<1>(item); }));

        auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - now)
                           .count();

        std::cout << "Storage2 batchWrite elpased: " << elpased << "ms" << std::endl;
        co_return;
    }());
}

void testStorage2SingleWrite(std::vector<std::tuple<std::string, storage::Entry>> const& dataSet)
{
    task::SequenceScheduler<false> scheduler;
    task::syncWait(
        [&dataSet]() -> task::Task<void> {
            storage2::concurrent_ordered_storage::ConcurrentOrderedStorage<false,
                std::tuple<std::string, std::string>, storage::Entry>
                storage;

            auto now = std::chrono::steady_clock::now();
            for (const auto& item : dataSet)
            {
                co_await storage.writeOne(
                    std::tuple<std::string, std::string>(std::string("table"), std::get<0>(item)),
                    std::get<1>(item));
            }
            auto elpased = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - now)
                               .count();

            std::cout << "Storage2 singleWrite elpased: " << elpased << "ms" << std::endl;
            co_return;
        }(),
        &scheduler);
}

void testKeyPageBatchWrite(
    std::vector<std::tuple<std::string, storage::Entry>> const& dataSet, size_t keyPageSize)
{}

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

    if (type == 0)
    {
        testStorage2BatchWrite(dataSet);
    }
    else
    {
        testStorage2SingleWrite(dataSet);
    }
}