#include <bcos-tars-protocol/impl/TarsHashable.h>
#include <bcos-tars-protocol/impl/TarsOutput.h>
#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-lightnode/ledger/LedgerImpl.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionMetaData.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <bcos-utilities/Ranges.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/tools/old/interface.hpp>
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
        ledger{storage};

    bcostars::Block block;
    ledger.getBlock<bcos::concepts::ledger::HEADER, bcos::concepts::ledger::TRANSACTIONS_METADATA,
        bcos::concepts::ledger::TRANSACTIONS, bcos::concepts::ledger::RECEIPTS>(10086, block);
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

    bcostars::Block block2;
    ledger.getBlock<bcos::concepts::ledger::ALL>(10086, block2);
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
        ledger.getBlock<bcos::concepts::ledger::HEADER>(10087, block3), std::runtime_error);
    BOOST_CHECK_THROW(
        ledger.getBlock<bcos::concepts::ledger::ALL>(10087, block3), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(setBlockAndGetInfo)
{
    LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, MockMemoryStorage> ledger{
        storage};

    bcostars::Block block;
    block.blockHeader.data.blockNumber = 100;

    for (auto i = 0u; i < count; ++i)
    {
        bcostars::Transaction transaction;
        transaction.data.blockLimit = 1000;
        transaction.data.to = "i am to";
        transaction.data.version = i;

        bcostars::TransactionReceipt receipt;
        receipt.data.contractAddress = "contract to";
        if (i >= 70)
        {
            receipt.data.status = -1;
        }

        block.transactions.emplace_back(std::move(transaction));
        block.receipts.emplace_back(std::move(receipt));
    }
    ledger.setTransactionsOrReceipts<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher>(
        block.transactions);

    bcos::concepts::hash::calculate<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher>(
        block, block.blockHeader.dataHash);

    BOOST_CHECK_NO_THROW(ledger.setBlock<bcos::concepts::ledger::ALL>(block));
    bcostars::Block gotBlock;
    ledger.getBlock<bcos::concepts::ledger::ALL>(100, gotBlock);

    BOOST_CHECK_EQUAL(gotBlock.blockHeader.data.blockNumber, block.blockHeader.data.blockNumber);
    BOOST_CHECK_EQUAL(gotBlock.transactions.size(), block.transactions.size());
    BOOST_CHECK_EQUAL(gotBlock.receipts.size(), block.receipts.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(gotBlock.transactions.begin(), gotBlock.transactions.end(),
        block.transactions.begin(), block.transactions.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(gotBlock.receipts.begin(), gotBlock.receipts.end(),
        block.receipts.begin(), block.receipts.end());

    std::array<std::byte, 32> hash;
    ledger.getBlockHashByNumber(100, hash);

    std::array<std::byte, 32> blockHash;
    bcos::concepts::hash::calculate<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher>(
        gotBlock, blockHash);

    int64_t newNumber = 100;
    ledger.getBlockNumberByHash(hash, newNumber);
    BOOST_CHECK_EQUAL(newNumber, 100);

    BOOST_CHECK_EQUAL(hash, blockHash);
}

BOOST_AUTO_TEST_CASE(notExistsBlock)
{
    LedgerImpl<bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, MockMemoryStorage> ledger{
        storage};

    std::array<std::byte, 32> hash{std::byte('0')};
    auto oldHash = hash;
    ledger.getBlockHashByNumber(50, hash);
    BOOST_CHECK_EQUAL(hash, oldHash);

    int64_t number = 0;
    ledger.getBlockNumberByHash(hash, number);
    BOOST_CHECK_EQUAL(number, -1);
}

BOOST_AUTO_TEST_CASE(ledgerSync)
{
    using Hasher = bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher;

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>> fromData;
    MockMemoryStorage fromStorage(fromData);
    LedgerImpl<Hasher, MockMemoryStorage> fromLedger{std::move(fromStorage)};

    std::map<std::tuple<std::string, std::string>, bcos::storage::Entry, std::less<>> toData;
    MockMemoryStorage toStorage(toData);
    LedgerImpl<Hasher, MockMemoryStorage> toLedger{std::move(toStorage)};

    bcostars::Block genesisBlock;
    genesisBlock.blockHeader.data.blockNumber = 0;
    fromLedger.setupGenesisBlock(genesisBlock);
    toLedger.setupGenesisBlock(genesisBlock);

    std::array<std::byte, 32> lastBlockHash;
    bcos::concepts::hash::calculate<Hasher>(genesisBlock, lastBlockHash);

    constexpr static size_t blockCount = 10;
    for (auto number = 1u; number < blockCount; ++number)
    {
        // write some block
        bcostars::Block block;
        block.blockHeader.data.blockNumber = number;

        bcostars::ParentInfo parentInfo;
        parentInfo.blockNumber = number - 1;
        bcos::concepts::bytebuffer::assignTo(lastBlockHash, parentInfo.blockHash);
        block.blockHeader.data.parentInfo.push_back(parentInfo);

        for (auto i = 0u; i < count; ++i)
        {
            bcostars::Transaction transaction;
            transaction.data.blockLimit = 1000;
            transaction.data.to = "i am to";
            transaction.data.version = i;

            bcostars::TransactionReceipt receipt;
            receipt.data.contractAddress = "contract to";
            if (i >= 70)
            {
                receipt.data.status = -1;
            }

            bcostars::TransactionMetaData metaData;
            bcos::concepts::hash::calculate<Hasher>(transaction, metaData.hash);

            block.transactionsMetaData.emplace_back(std::move(metaData));
            block.transactions.emplace_back(std::move(transaction));
            block.receipts.emplace_back(std::move(receipt));

            bcos::crypto::merkle::Merkle<Hasher> merkler;
        }
        fromLedger.setTransactionsOrReceipts<Hasher>(block.transactions);
        toLedger.setTransactionsOrReceipts<Hasher>(block.transactions);

        BOOST_CHECK_NO_THROW(fromLedger.setBlock<bcos::concepts::ledger::ALL>(block));
        bcos::concepts::hash::calculate<Hasher>(block, lastBlockHash);
    }

    toLedger.sync<decltype(fromLedger), bcostars::Block>(fromLedger, false);

    // get all block
    std::vector<bcostars::Block> fromBlocks(blockCount);
    std::vector<bcostars::Block> toBlocks(blockCount);
    for (auto i = 1u; i < blockCount; ++i)
    {
        fromLedger.getBlock<bcos::concepts::ledger::ALL>(i, fromBlocks[i]);
        toLedger.getBlock<bcos::concepts::ledger::ALL>(i, toBlocks[i]);
    }

    BOOST_CHECK_EQUAL_COLLECTIONS(
        fromBlocks.begin(), fromBlocks.end(), toBlocks.begin(), toBlocks.end());
}

BOOST_AUTO_TEST_SUITE_END()