
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "bcos-ledger/bcos-ledger/LedgerImpl.h"
#include "impl/TarsSerializable.h"
#include "tars/Transaction.h"
#include "tars/TransactionReceipt.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <optional>
#include <ranges>

using namespace bcos::ledger;

struct MockMemoryStorage
{
    std::optional<bcos::storage::Entry> getRow(
        [[maybe_unused]] std::string_view table, [[maybe_unused]] std::string_view key)
    {
        auto entryIt = data.find(std::tuple{table, key});
        if (entryIt != data.end())
        {
            return entryIt->second;
        }
        return {};
    }

    std::vector<std::optional<bcos::storage::Entry>> getRows(
        [[maybe_unused]] std::string_view table,
        [[maybe_unused]] std::ranges::range auto const& keys)
    {
        std::vector<std::optional<bcos::storage::Entry>> output;
        output.reserve(std::size(keys));
        for (auto&& key : keys)
        {
            output.emplace_back(getRow(table, key));
        }
        return output;
    }

    void setRow(std::string_view table, std::string_view key, bcos::storage::Entry entry)
    {
        auto it = data.find(std::tuple{table, key});
        if (it != data.end())
        {
            it->second = std::move(entry);
        }
        else
        {
            data.emplace(std::tuple{std::string{table}, std::string{key}}, std::move(entry));
        }
    }

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
        header.data.gasUsed = "1000";
        header.data.timestamp = 5000;

        bcos::storage::Entry headerEntry;
        auto buffer = bcos::concepts::serialize::encode(header);
        headerEntry.setField(0, std::move(buffer));
        data.emplace(std::tuple{SYS_NUMBER_2_BLOCK_HEADER, "10086"}, std::move(headerEntry));

        bcostars::Block transactionsBlock;
        transactionsBlock.transactionsMetaData.resize(count);
        for (auto i = 0u; i < count; ++i)
        {
            std::string hashStr = "hash_" + boost::lexical_cast<std::string>(i);
            transactionsBlock.transactionsMetaData[i].hash.assign(hashStr.begin(), hashStr.end());

            // transaction
            decltype(transactionsBlock.transactions)::value_type transaction;
            transaction.data.chainID = "chain";
            transaction.data.groupID = "group";
            transaction.importTime = 1000;

            auto txBuffer = bcos::concepts::serialize::encode(transaction);
            bcos::storage::Entry txEntry;
            txEntry.setField(0, std::move(txBuffer));

            data.emplace(std::tuple{SYS_HASH_2_TX, hashStr}, std::move(txEntry));

            // receipt
            decltype(transactionsBlock.receipts)::value_type receipt;
            receipt.data.blockNumber = 10086;
            receipt.data.contractAddress = "contract";

            auto receiptBuffer = bcos::concepts::serialize::encode(receipt);
            bcos::storage::Entry receiptEntry;
            receiptEntry.setField(0, std::move(receiptBuffer));
            data.emplace(std::tuple{SYS_HASH_2_RECEIPT, hashStr}, std::move(receiptEntry));
        }

        auto txsBuffer = bcos::concepts::serialize::encode(transactionsBlock);
        bcos::storage::Entry txsEntry;
        txsEntry.setField(0, std::move(txsBuffer));
        data.emplace(std::tuple{SYS_NUMBER_2_TXS, "10086"}, std::move(txsEntry));
    }

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>> data;
    MockMemoryStorage storage;

    constexpr static size_t count = 100;
};

BOOST_FIXTURE_TEST_SUITE(LedgerImplTest, LedgerImplFixture)

BOOST_AUTO_TEST_CASE(getBlock)
{
    LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, MockMemoryStorage,
        bcostars::Block>
        ledger{storage};

    auto [header, transactions, receipts] =
        ledger.getBlock<BLOCK_HEADER, BLOCK_TRANSACTIONS, BLOCK_RECEIPTS>(10086);
    BOOST_CHECK_EQUAL(header.data.blockNumber, 10086);
    BOOST_CHECK_EQUAL(header.data.gasUsed, "1000");
    BOOST_CHECK_EQUAL(header.data.timestamp, 5000);

    BOOST_CHECK_EQUAL(transactions.size(), count);
    BOOST_CHECK_EQUAL(receipts.size(), count);

    for (auto i = 0u; i < count; ++i)
    {
        BOOST_CHECK_EQUAL(transactions[i].data.chainID, "chain");
        BOOST_CHECK_EQUAL(transactions[i].data.groupID, "group");
        BOOST_CHECK_EQUAL(transactions[i].importTime, 1000);

        BOOST_CHECK_EQUAL(receipts[i].data.blockNumber, 10086);
        BOOST_CHECK_EQUAL(receipts[i].data.contractAddress, "contract");
    }

    auto [block] = ledger.getBlock<BLOCK_ALL>(10086);
    BOOST_CHECK_EQUAL(block.blockHeader.data.blockNumber, 10086);
    BOOST_CHECK_EQUAL(block.blockHeader.data.gasUsed, "1000");
    BOOST_CHECK_EQUAL(block.blockHeader.data.timestamp, 5000);

    BOOST_CHECK_EQUAL(block.transactions.size(), count);
    BOOST_CHECK_EQUAL(block.receipts.size(), count);

    for (auto i = 0u; i < count; ++i)
    {
        BOOST_CHECK_EQUAL(block.transactions[i].data.chainID, "chain");
        BOOST_CHECK_EQUAL(block.transactions[i].data.groupID, "group");
        BOOST_CHECK_EQUAL(block.transactions[i].importTime, 1000);

        BOOST_CHECK_EQUAL(block.receipts[i].data.blockNumber, 10086);
        BOOST_CHECK_EQUAL(block.receipts[i].data.contractAddress, "contract");
    }

    BOOST_CHECK_THROW(ledger.getBlock<BLOCK_HEADER>(10087), std::runtime_error);
    BOOST_CHECK_THROW(ledger.getBlock<BLOCK_TRANSACTIONS>(10087), std::runtime_error);
    BOOST_CHECK_THROW(ledger.getBlock<BLOCK_RECEIPTS>(10087), std::runtime_error);
    BOOST_CHECK_THROW(ledger.getBlock<BLOCK_ALL>(10087), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(setBlock)
{
    LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, MockMemoryStorage,
        bcostars::Block>
        ledger{storage};

    bcostars::Block block;
    block.blockHeader.data.blockNumber = 100;

    for (auto i = 0u; i < count; ++i)
    {
        bcostars::Transaction transaction;
        transaction.data.blockLimit = 1000;
        transaction.data.to = "i am to";

        bcostars::TransactionReceipt receipt;
        receipt.data.contractAddress = "contract to";
        if (i >= 70)
        {
            receipt.data.status = -1;
        }

        block.transactions.emplace_back(std::move(transaction));
        block.receipts.emplace_back(std::move(receipt));
    }
    ledger.setTransactionsOrReceipts(block.transactions);

    BOOST_CHECK_NO_THROW(ledger.setBlockWithoutTransaction(storage, std::move(block)));
    auto [gotBlock] = ledger.getBlock<BLOCK_ALL>(100);

    BOOST_CHECK_EQUAL(gotBlock.blockHeader.data.blockNumber, block.blockHeader.data.blockNumber);
    BOOST_CHECK_EQUAL(gotBlock.transactionsMetaData.size(), block.transactions.size());
    BOOST_CHECK_EQUAL(gotBlock.transactions.size(), block.transactions.size());
    BOOST_CHECK_EQUAL(gotBlock.receiptsHash.size(), block.receipts.size());
    BOOST_CHECK_EQUAL(gotBlock.receipts.size(), block.receipts.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(gotBlock.transactions.begin(), gotBlock.transactions.end(),
        block.transactions.begin(), block.transactions.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(gotBlock.receipts.begin(), gotBlock.receipts.end(),
        block.receipts.begin(), block.receipts.end());
}

BOOST_AUTO_TEST_SUITE_END()