#include "bcos-ledger/bcos-ledger/LedgerImpl.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/unit_test.hpp>
#include <ranges>

using namespace bcos::ledger;

struct MockMemoryStorage
{
    bcos::storage::Entry getRow([[maybe_unused]] std::string_view table, [[maybe_unused]] std::string_view key)
    {
        return {};
    }
    std::vector<bcos::storage::Entry> getRows(
        [[maybe_unused]] std::string_view table, [[maybe_unused]] std::ranges::range auto const& keys)
    {
        return {};
    }
    void setRow([[maybe_unused]] std::string_view table, [[maybe_unused]] std::string_view key,
        [[maybe_unused]] bcos::storage::Entry entry)
    {}
    void createTable([[maybe_unused]] std::string_view tableName) {}
};

class LedgerImplFixture
{
};

BOOST_FIXTURE_TEST_SUITE(LedgerImplTest, LedgerImplFixture)

BOOST_AUTO_TEST_CASE(getBlock)
{
    LedgerImpl<MockMemoryStorage, bcostars::Block> ledger{MockMemoryStorage{}};

    std::string hash = "Hello world!";
    [[maybe_unused]] auto [num1, num2] = ledger.getBlock<BLOCK_HEADER, BLOCK_TRANSACTIONS>(hash);

    std::cout << "num is : " << num1 << ", " << num2 << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()