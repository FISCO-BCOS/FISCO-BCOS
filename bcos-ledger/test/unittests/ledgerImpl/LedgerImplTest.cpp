#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "bcos-ledger/bcos-ledger/LedgerImpl.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <ranges>

using namespace bcos::ledger;

struct MockMemoryStorage
{
    std::optional<bcos::storage::Entry> getRow(
        [[maybe_unused]] std::string_view table, [[maybe_unused]] std::string_view key)
    {
        auto entryIt = data.find(std::tuple{table, key});
        if (entryIt != data.end()) { return entryIt->second; }
        return {};
    }

    std::vector<std::optional<bcos::storage::Entry>> getRows(
        [[maybe_unused]] std::string_view table, [[maybe_unused]] std::ranges::range auto const& keys)
    {
        std::vector<std::optional<bcos::storage::Entry>> output;
        output.reserve(std::size(keys));
        for (auto&& it : keys)
        {
            output.emplace_back(getRow(table, it));
        }
        return output;
    }

    void setRow([[maybe_unused]] std::string_view table, [[maybe_unused]] std::string_view key,
        [[maybe_unused]] bcos::storage::Entry entry)
    {}

    void createTable([[maybe_unused]] std::string_view tableName) {}

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>>& data;
};

struct LedgerImplFixture
{
    LedgerImplFixture() : storage{.data = data}
    {
        // Put some entry into data
        bcostars::BlockHeader header;
        header.data.blockNumber = 10086;


        bcos::storage::Entry blockHeader;
        data.emplace(std::tuple{SYS_NUMBER_2_BLOCK_HEADER, "10086", std::move(blockHeader)});
    }

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>> data;
    MockMemoryStorage storage;
};

BOOST_FIXTURE_TEST_SUITE(LedgerImplTest, LedgerImplFixture)

BOOST_AUTO_TEST_CASE(getBlock)
{
    LedgerImpl<MockMemoryStorage, bcostars::Block> ledger{storage};

    [[maybe_unused]] auto [header, transactions] = ledger.getBlock<BLOCK_HEADER, BLOCK_TRANSACTIONS>(10086);

    // std::cout << "num is : " << num1 << ", " << num2 << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()