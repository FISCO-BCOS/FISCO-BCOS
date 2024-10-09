#include "bcos-ledger/LedgerImpl.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-tars-protocol/impl/TarsHashable.h>
#include <bcos-tars-protocol/impl/TarsOutput.h>
#include <bcos-tars-protocol/impl/TarsSerializable.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionMetaData.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Ranges.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>
#include <optional>

using namespace bcos::ledger;

namespace std
{
ostream& operator<<(ostream& os, std::vector<tars::Char> const& buffer)
{
    auto hexBuffer = boost::algorithm::hex_lower(buffer);
    os << string_view{(const char*)hexBuffer.data(), hexBuffer.size()};
    return os;
}

ostream& operator<<(
    ostream& os, std::pair<std::tuple<std::string, std::string>, bcos::storage::Entry> const& value)
{
    auto hexBuffer = boost::algorithm::hex_lower(std::string(value.second.get()));
    os << std::get<0>(value.first) << ":" << std::get<1>(value.first) << " - " << hexBuffer;
    return os;
}

ostream& operator<<(ostream& os, std::array<std::byte, 32> const& buffer)
{
    std::string hex;
    boost::algorithm::hex_lower((const char*)buffer.data(),
        (const char*)buffer.data() + buffer.size(), std::back_inserter(hex));
    os << hex;
    return os;
}

ostream& operator<<(ostream& os, bcos::storage::Entry const&)
{
    return os;
}
}  // namespace std

struct MockMemoryStorage : bcos::concepts::storage::StorageBase<MockMemoryStorage>
{
    MockMemoryStorage(
        std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>>& data1)
      : bcos::concepts::storage::StorageBase<MockMemoryStorage>(), data(data1){};

    std::optional<bcos::storage::Entry> impl_getRow(std::string_view table, std::string_view key)
    {
        auto entryIt = data.find(std::tuple{table, key});
        if (entryIt != data.end())
        {
            return entryIt->second;
        }
        return {};
    }

    std::vector<std::optional<bcos::storage::Entry>> impl_getRows(
        std::string_view table, RANGES::range auto const& keys)
    {
        std::vector<std::optional<bcos::storage::Entry>> output;
        output.reserve(RANGES::size(keys));
        for (auto&& key : keys)
        {
            output.emplace_back(getRow(table, bcos::concepts::bytebuffer::toView(key)));
        }
        return output;
    }

    void impl_setRow(std::string_view table, std::string_view key, bcos::storage::Entry entry)
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

    void impl_createTable([[maybe_unused]] std::string_view tableName) {}

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>>& data;
};

struct LedgerImplFixture
{
    LedgerImplFixture() : storage{data}
    {
        // Put some entry into data
        bcostars::BlockHeader header;
        header.data.blockNumber = 10086;
        header.data.gasUsed = "1000";
        header.data.timestamp = 5000;

        bcos::storage::Entry headerEntry;
        std::vector<bcos::byte> headerBuffer;
        bcos::concepts::serialize::encode(header, headerBuffer);
        headerEntry.setField(0, std::move(headerBuffer));
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

            std::vector<bcos::byte> txBuffer;
            bcos::concepts::serialize::encode(transaction, txBuffer);
            bcos::storage::Entry txEntry;
            txEntry.setField(0, std::move(txBuffer));

            data.emplace(std::tuple{SYS_HASH_2_TX, hashStr}, std::move(txEntry));

            // receipt
            decltype(transactionsBlock.receipts)::value_type receipt;
            receipt.data.blockNumber = 10086;
            receipt.data.contractAddress = "contract";

            std::vector<bcos::byte> receiptBuffer;
            bcos::concepts::serialize::encode(receipt, receiptBuffer);
            bcos::storage::Entry receiptEntry;
            receiptEntry.setField(0, std::move(receiptBuffer));
            data.emplace(std::tuple{SYS_HASH_2_RECEIPT, hashStr}, std::move(receiptEntry));
        }

        std::vector<bcos::byte> txsBuffer;
        bcos::concepts::serialize::encode(transactionsBlock, txsBuffer);
        bcos::storage::Entry txsEntry;
        txsEntry.setField(0, std::move(txsBuffer));
        data.emplace(std::tuple{SYS_NUMBER_2_TXS, "10086"}, std::move(txsEntry));

        bcostars::Block nonceBlock;
        nonceBlock.nonceList.emplace_back(std::string("i am a nonce"));

        std::vector<bcos::byte> nonceBuffer;
        bcos::concepts::serialize::encode(nonceBlock, nonceBuffer);
        bcos::storage::Entry nonceEntry;
        nonceEntry.setField(0, std::move(nonceBuffer));
        data.emplace(std::tuple{SYS_BLOCK_NUMBER_2_NONCES, "10086"}, std::move(nonceEntry));
    }

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>> data;
    MockMemoryStorage storage;

    constexpr static size_t count = 100;
};

BOOST_FIXTURE_TEST_SUITE(LedgerImplTest, LedgerImplFixture)

BOOST_AUTO_TEST_CASE(getBlock)
{
    bcos::ledger::LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, MockMemoryStorage>
        ledger{
            bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher{}, storage, nullptr, nullptr, 1000};

    bcostars::Block block;
    bcos::task::syncWait(
        ledger
            .getBlock<bcos::concepts::ledger::HEADER, bcos::concepts::ledger::TRANSACTIONS_METADATA,
                bcos::concepts::ledger::TRANSACTIONS, bcos::concepts::ledger::RECEIPTS>(
                10086, block));
    BOOST_CHECK_EQUAL(block.blockHeader.data.blockNumber, 10086);
    BOOST_CHECK_EQUAL(block.blockHeader.data.gasUsed, "1000");
    BOOST_CHECK_EQUAL(block.blockHeader.data.timestamp, 5000);

    BOOST_CHECK_EQUAL(block.transactions.size(), count);
    BOOST_CHECK_EQUAL(block.receipts.size(), count);

    for (auto i = 0U; i < count; ++i)
    {
        BOOST_CHECK_EQUAL(block.transactions[i].data.chainID, "chain");
        BOOST_CHECK_EQUAL(block.transactions[i].data.groupID, "group");
        BOOST_CHECK_EQUAL(block.transactions[i].importTime, 1000);

        BOOST_CHECK_EQUAL(block.receipts[i].data.blockNumber, 10086);
        BOOST_CHECK_EQUAL(block.receipts[i].data.contractAddress, "contract");
    }

    bcostars::Block block2;
    bcos::task::syncWait(ledger.getBlock<bcos::concepts::ledger::ALL>(10086, block2));
    BOOST_CHECK_EQUAL(block2.blockHeader.data.blockNumber, 10086);
    BOOST_CHECK_EQUAL(block2.blockHeader.data.gasUsed, "1000");
    BOOST_CHECK_EQUAL(block2.blockHeader.data.timestamp, 5000);

    BOOST_CHECK_EQUAL(block2.transactions.size(), count);
    BOOST_CHECK_EQUAL(block2.receipts.size(), count);

    for (auto i = 0u; i < count; ++i)
    {
        BOOST_CHECK_EQUAL(block2.transactions[i].data.chainID, "chain");
        BOOST_CHECK_EQUAL(block2.transactions[i].data.groupID, "group");
        BOOST_CHECK_EQUAL(block2.transactions[i].importTime, 1000);

        BOOST_CHECK_EQUAL(block2.receipts[i].data.blockNumber, 10086);
        BOOST_CHECK_EQUAL(block2.receipts[i].data.contractAddress, "contract");
    }

    bcostars::Block block3;
    BOOST_CHECK_THROW(
        bcos::task::syncWait(ledger.getBlock<bcos::concepts::ledger::HEADER>(10087, block3)),
        bcos::ledger::NotFoundBlockHeader);
    BOOST_CHECK_THROW(
        bcos::task::syncWait(ledger.getBlock<bcos::concepts::ledger::ALL>(10087, block3)),
        bcos::ledger::GetBlockDataError);
}

BOOST_AUTO_TEST_CASE(notExistsBlock)
{
    LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, MockMemoryStorage> ledger{
        bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher{}, storage, nullptr, nullptr, 1000};

    std::vector<std::byte> hash;
    bcos::task::syncWait(ledger.getBlockHashByNumber(50, hash));
    BOOST_CHECK(RANGES::empty(hash));

    int64_t number = 0;
    bcos::task::syncWait(ledger.getBlockNumberByHash(hash, number));
    BOOST_CHECK_EQUAL(number, -1);
}

BOOST_AUTO_TEST_SUITE_END()
