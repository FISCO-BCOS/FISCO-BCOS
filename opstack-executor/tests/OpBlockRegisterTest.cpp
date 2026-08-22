// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockRegisterTest — direct unit tests for the pure-function sink of the engine's
// registerOpBlock: `bcos::evm::engine::opstackRegisterBlock`. Five-table write correctness
// (SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER / SYS_HASH_2_RECEIPT /
// SYS_HASH_2_TX) is the highest-risk point of the OP driver orchestration alignment plan, so it is
// pinned here without EngineService orchestration: the injected EnvelopeToTarsConverter is a fake
// lambda, isolating the table-write logic from the engine/RPC link the production lambda needs
// (opstack-executor must not link bcos-engine / bcos-rpc).

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/engine/Errors.h>  // OpExecutionInternalError (direct include, not via EngineServiceImpl.h)
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>  // syncWait
#include <bcos-utilities/Common.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
// OpBlockRegister.h placed after Entry/ByteBuffer/StateKey (its own includes cover them too).
#include <opstack-executor/OpBlockRegister.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
// ── storage fixture (alias family verbatim from OpNewPayloadRpcE2eTest.cpp:41-75) ──
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

// ── composition-root stand-ins (same construction as OpNewPayloadRpcE2eTest.cpp:106-128) ──
bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

/// Minimal OP header (same field set as OpSchedulerImplSmokeTest.cpp:65-88): every optional field
/// the tars BlockHeader encode touches is populated so encode()/decode round-trips cleanly.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeOpHeader(
    bcos::protocol::BlockNumber number)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(1700000000000);
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(30000000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1000000000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// A fake injected EnvelopeToTarsConverter: mirrors production `opEnvelopeToTars`
/// (EngineServiceImpl.cpp:45) by pinning extraTransactionHash to the passed txHash — that is what
/// makes the round-trip assertion `tx->hash() == txHash` meaningful (the read side's tx.hash()
/// returns extraTransactionHash for a Web3-typed tx, TransactionImpl.cpp:80-85; a type-0 tx's
/// hash() would return the BCOS dataHash instead). sender is arbitrary raw bytes.
bcos::evm::engine::EnvelopeToTarsConverter fixedTarsConverter()
{
    return [](bcos::bytes const&,
               bcos::crypto::HashType const& txHash) -> std::optional<bcostars::Transaction> {
        bcostars::Transaction tarsTx;
        tarsTx.type = static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
        tarsTx.sender = {0x11, 0x22, 0x33};
        tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
        return tarsTx;
    };
}

/// Converter that reports a malformed/un-enumerated envelope -> row skipped, block stays valid.
bcos::evm::engine::EnvelopeToTarsConverter nullTarsConverter()
{
    return [](bcos::bytes const&,
               bcos::crypto::HashType const&) -> std::optional<bcostars::Transaction> {
        return std::nullopt;
    };
}

const bcos::h256 c_blockHash{
    std::string{"0x112233445566778899aabbccddeeff00112233445566778899aabbccddeeff0011"}};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpBlockRegisterSuite)

BOOST_AUTO_TEST_CASE(HappyFiveTables)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    auto view = multiLayerStorage.fork();
    view.newMutable();

    auto blockFactory = makeBlockFactory();
    auto header = makeOpHeader(1);
    bcos::bytes rawTx{0x7e, 0x02, 0x03};  // fake envelope — the converter never decodes it
    std::vector<bcos::bytes> rawTxBytes{rawTx};

    auto receipt = blockFactory->receiptFactory()->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, 0, bcos::bytesConstRef{}, /*blockNumber=*/1);

    bcos::evm::engine::OpExecuteBlockResult result{};
    result.receipts = {receipt};

    bcos::task::syncWait(bcos::evm::engine::opstackRegisterBlock(
        view, *header, c_blockHash, rawTxBytes, result, *blockFactory, fixedTarsConverter()));

    // 1. SYS_NUMBER_2_HASH["1"] == blockHash bytes
    const auto blockNumberStr = boost::lexical_cast<std::string>(header->number());
    auto num2hash = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr}));
    BOOST_REQUIRE(num2hash.has_value());
    BOOST_CHECK_EQUAL(num2hash->get(),
        std::string_view(reinterpret_cast<const char*>(c_blockHash.data()), c_blockHash.size()));

    // 2. SYS_HASH_2_NUMBER[blockHash] == "1"
    auto hash2num = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                          bcos::concepts::bytebuffer::toView(c_blockHash)}));
    BOOST_REQUIRE(hash2num.has_value());
    BOOST_CHECK_EQUAL(hash2num->get(), blockNumberStr);

    // 3. SYS_NUMBER_2_BLOCK_HEADER["1"] == header.encode(); decode round-trips number()==1
    auto headerEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}));
    BOOST_REQUIRE(headerEntry.has_value());
    bcos::bytes expectedHeaderBytes;
    header->encode(expectedHeaderBytes);
    BOOST_CHECK_EQUAL(headerEntry->get(),
        std::string_view(
            reinterpret_cast<const char*>(expectedHeaderBytes.data()), expectedHeaderBytes.size()));
    auto roundTripHeader = blockFactory->blockHeaderFactory()->createBlockHeader(
        bcos::bytes(headerEntry->get().begin(), headerEntry->get().end()));
    BOOST_CHECK_EQUAL(roundTripHeader->number(), header->number());

    // 4. SYS_HASH_2_RECEIPT[txHash] == receipt->encode()
    auto& hashImpl = *blockFactory->cryptoSuite()->hashImpl();
    const auto txHash = hashImpl.hash(rawTxBytes[0]);
    auto receiptEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)}));
    BOOST_REQUIRE(receiptEntry.has_value());
    bcos::bytes expectedReceiptBytes;
    receipt->encode(expectedReceiptBytes);
    BOOST_CHECK_EQUAL(receiptEntry->get(),
        std::string_view(reinterpret_cast<const char*>(expectedReceiptBytes.data()),
            expectedReceiptBytes.size()));

    // 5. SYS_HASH_2_TX[txHash] present; round-trip: createTransaction -> hash()==txHash
    //    (read-side extraTransactionHash, per EngineServiceImpl.cpp:43 comment)
    auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
    BOOST_REQUIRE(txEntry.has_value());
    auto txBytes = bcos::bytesConstRef(
        reinterpret_cast<bcos::byte const*>(txEntry->get().data()), txEntry->get().size());
    auto tx = blockFactory->transactionFactory()->createTransaction(
        txBytes, /*checkSig=*/false, /*checkHash=*/false, /*tainted=*/false);
    BOOST_CHECK_EQUAL(tx->hash(), txHash);

    // 6. SYS_CURRENT_STATE[SYS_KEY_CURRENT_NUMBER] == blockNumberStr — eth_blockNumber parity
    //    (Ledger::asyncPrewriteBlock also advances this row; without it eth_blockNumber stays 0).
    auto curNumber = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER}));
    BOOST_REQUIRE(curNumber.has_value());
    BOOST_CHECK_EQUAL(curNumber->get(), blockNumberStr);

    // 7. SYS_NUMBER_2_TXS[blockNumber] decodes to a block whose tx hashes match the receipt keys —
    //    the legacy read path (getBlockData RECEIPTS/TRANSACTIONS_HASH) needs this metadata list.
    auto txMetaEntry = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_TXS, blockNumberStr}));
    BOOST_REQUIRE(txMetaEntry.has_value());
    auto metaBytes = bcos::bytesConstRef(
        reinterpret_cast<bcos::byte const*>(txMetaEntry->get().data()), txMetaEntry->get().size());
    auto metaBlock = blockFactory->createBlock(metaBytes);
    BOOST_REQUIRE_EQUAL(metaBlock->transactionsMetaDataSize(), 1u);
    BOOST_CHECK_EQUAL(metaBlock->transactionHash(0), txHash);
}

BOOST_AUTO_TEST_CASE(ReceiptCountMismatch)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    auto view = multiLayerStorage.fork();
    view.newMutable();

    auto blockFactory = makeBlockFactory();
    auto header = makeOpHeader(1);
    // 2 raw envelopes but only 1 receipt -> the receipt-count invariant must fire.
    std::vector<bcos::bytes> rawTxBytes{{0x7e, 0x02, 0x03}, {0x7e, 0x02, 0x04}};
    auto receipt = blockFactory->receiptFactory()->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, 0, bcos::bytesConstRef{}, /*blockNumber=*/1);

    bcos::evm::engine::OpExecuteBlockResult result{};
    result.receipts = {receipt};

    BOOST_CHECK_THROW(bcos::task::syncWait(bcos::evm::engine::opstackRegisterBlock(view, *header,
                          c_blockHash, rawTxBytes, result, *blockFactory, fixedTarsConverter())),
        bcos::engine::OpExecutionInternalError);
}

BOOST_AUTO_TEST_CASE(NullReceipt)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    auto view = multiLayerStorage.fork();
    view.newMutable();

    auto blockFactory = makeBlockFactory();
    auto header = makeOpHeader(1);
    std::vector<bcos::bytes> rawTxBytes{{0x7e, 0x02, 0x03}};

    bcos::evm::engine::OpExecuteBlockResult result{};
    result.receipts = {nullptr};

    BOOST_CHECK_THROW(bcos::task::syncWait(bcos::evm::engine::opstackRegisterBlock(view, *header,
                          c_blockHash, rawTxBytes, result, *blockFactory, fixedTarsConverter())),
        bcos::engine::OpExecutionInternalError);
}

BOOST_AUTO_TEST_CASE(ConverterNulloptSkipsTxRow)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    auto view = multiLayerStorage.fork();
    view.newMutable();

    auto blockFactory = makeBlockFactory();
    auto header = makeOpHeader(1);
    bcos::bytes rawTx{0x7e, 0x02, 0x03};
    std::vector<bcos::bytes> rawTxBytes{rawTx};

    auto receipt = blockFactory->receiptFactory()->createReceipt(bcos::u256(21000), std::string{},
        std::vector<bcos::protocol::LogEntry>{}, 0, bcos::bytesConstRef{}, /*blockNumber=*/1);

    bcos::evm::engine::OpExecuteBlockResult result{};
    result.receipts = {receipt};

    // Converter reports nullopt: block stays valid, that tx is simply not addressable by hash.
    bcos::task::syncWait(bcos::evm::engine::opstackRegisterBlock(
        view, *header, c_blockHash, rawTxBytes, result, *blockFactory, nullTarsConverter()));

    const auto blockNumberStr = boost::lexical_cast<std::string>(header->number());
    auto& hashImpl = *blockFactory->cryptoSuite()->hashImpl();
    const auto txHash = hashImpl.hash(rawTxBytes[0]);

    // The four other tables still carry their rows.
    auto num2hash = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr}));
    BOOST_REQUIRE(num2hash.has_value());
    auto hash2num = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                          bcos::concepts::bytebuffer::toView(c_blockHash)}));
    BOOST_REQUIRE(hash2num.has_value());
    auto headerEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}));
    BOOST_REQUIRE(headerEntry.has_value());
    auto receiptEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)}));
    BOOST_REQUIRE(receiptEntry.has_value());

    // SYS_HASH_2_TX[txHash] absent.
    auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
    BOOST_CHECK(!txEntry.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
