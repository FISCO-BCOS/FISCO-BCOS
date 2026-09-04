// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
//
// Matrix: S5 — OpEngineService wired on release-3.18.0 (single transactions[i].raw carrier).
// Full dual parity vs EngineServiceImpl OP mode / GoldenSample e2e is deferred (no Impl
// opMode and no t8n fixtures on this branch). This suite covers:
//   - OpEngineService API gates (capabilities, V3 newPayload, gasLimit)
//   - Shared FCU ordering exceptions vs EngineServiceImpl (safe/finalized)
//   - EngineTracker exclusive/shared publish concurrency (op_fast_path)

#include "engine/bcos-engine/EngineServiceImpl.h"
#include "engine/bcos-engine/EngineTracker.h"
#include "engine/bcos-engine/OpEngineService.h"

#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/engine/EngineService.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/Exceptions.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/tests/OpSchedulerSeamTestHelpers.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <functional>
#include <latch>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace op_engine_parity_test
{

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

struct StubMemPool
{
    std::vector<bcos::crypto::HashType> removed;
    std::vector<bcos::protocol::Transaction::Ptr> pool;
    void removeByHash(std::span<bcos::crypto::HashType const> hashes)
    {
        removed.insert(removed.end(), hashes.begin(), hashes.end());
    }
    template <class View>
    void remove(View&)
    {}
    template <class View, class OutputIt>
    void seal(int64_t limit, View&, OutputIt out)
    {
        auto const n = std::min<int64_t>(limit, static_cast<int64_t>(pool.size()));
        for (int64_t i = 0; i < n; ++i)
        {
            *out++ = pool[static_cast<std::size_t>(i)];
        }
    }
};

/// First executeBlock fails with a structured culprit; later calls succeed so the
/// build retry loop can finish. Used to drive BH (capacity, no evict) and BC (evict).
struct RecordingScheduler : bcos::scheduler::SchedulerInterface
{
    bcos::h256 culprit;
    bool rejectAsCapacity = false;
    bool failFirst = true;
    int executeCalls = 0;
    bcos::protocol::BlockHeaderFactory::Ptr headerFactory;

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++executeCalls;
        if (failFirst && executeCalls == 1)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            if (rejectAsCapacity)
            {
                *error << bcos::engine::OpRejectIsCapacity(true);
            }
            callback(std::move(error), nullptr, false);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        header->setWithdrawalsRoot(bcos::h256{});
        header->setBlobGasUsed(0);
        callback(nullptr, std::move(header), false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        callback(nullptr, nullptr);
    }
    void status(std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)>) override
    {}
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)>) override
    {}
    void reset(std::function<void(bcos::Error::Ptr)> callback) override { callback(nullptr); }
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)>) override {}
    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)>) override
    {}
};

/// Rejects `culprit` whenever that hash is in the block; if only `successor` remains,
/// rejects it as a non-capacity nonce-gap (the R3-F1 production shape).
/// `culprit`/`successor` are pool hashes (OpCulpritTxHash). `*EnvHash` are
/// keccak(reassembled envelope), matching buildOpBlock's transactionHash.
struct NonceChainScheduler : RecordingScheduler
{
    bcos::h256 successor;
    bcos::h256 culpritEnvHash;
    bcos::h256 successorEnvHash;

    void executeBlock(bcos::protocol::Block::Ptr block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++executeCalls;
        bool hasCulprit = false;
        bool hasSuccessor = false;
        if (block)
        {
            for (auto txView : block->transactions())
            {
                auto tx = std::move(txView).toShared();
                if (!tx)
                {
                    continue;
                }
                auto const hash = tx->hash();
                if (hash == culpritEnvHash)
                {
                    hasCulprit = true;
                }
                if (hash == successorEnvHash)
                {
                    hasSuccessor = true;
                }
            }
        }
        if (hasCulprit)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: reject sealed tx");
            *error << bcos::engine::OpCulpritTxHash(culprit);
            if (rejectAsCapacity)
            {
                *error << bcos::engine::OpRejectIsCapacity(true);
            }
            callback(std::move(error), nullptr, false);
            return;
        }
        if (hasSuccessor)
        {
            auto error = BCOS_ERROR_PTR(-1, "op block: nonce gap");
            *error << bcos::engine::OpCulpritTxHash(successor);
            callback(std::move(error), nullptr, false);
            return;
        }
        auto header = headerFactory->createBlockHeader();
        header->setStateRoot(bcos::h256{});
        header->setReceiptsRoot(bcos::h256{});
        header->setGasUsed(0);
        header->setWithdrawalsRoot(bcos::h256{});
        header->setBlobGasUsed(0);
        callback(nullptr, std::move(header), false);
    }
};

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

/// EIP-1559 envelope that opEnvelopeToTars can decode, plus the signing payload +
/// 65-byte signature that reassembleWeb3RawTransaction expects on the seal path.
struct DecodableWeb3Tx
{
    bcos::protocol::Transaction::Ptr tx;
    std::string rawHex;
};

static bcos::h256 envelopeHashOf(bcos::protocol::Transaction::Ptr const& tx)
{
    bcos::crypto::Keccak256 hasher;
    auto const raw = bcostars::protocol::reassembleWeb3RawTransaction(
        tx->extraTransactionBytes(), tx->signatureData());
    return hasher.hash(bcos::ref(raw));
}

static DecodableWeb3Tx makeDecodableWeb3Tx(
    uint64_t nonce, bcos::crypto::KeyPairInterface* keyPair = nullptr)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = nonce;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 1;
    w3.gasLimit = 21000;
    w3.to = bcos::Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd");
    w3.value = 0;
    bcos::crypto::Secp256k1Crypto secp;
    auto owned = keyPair == nullptr ? secp.generateKeyPair() : nullptr;
    auto const& kp = keyPair != nullptr ? *keyPair : *owned;
    auto const sig = secp.sign(kp, w3.hashForSign(), false);
    BOOST_REQUIRE(sig);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];

    auto const raw = w3.encode();
    auto const signPayload = w3.encodeForSign();
    {
        bcos::rpc::Web3Transaction decoded;
        bcos::bytes copy = raw;
        bcos::bytesRef ref{copy.data(), copy.size()};
        auto err = bcos::codec::rlp::decode(ref, decoded);
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE(ref.empty());
        BOOST_REQUIRE(bcos::engine::engine_common::op::opEnvelopeToTars(raw, bcos::h256{}));
    }
    bcos::bytes signature(65, 0);
    std::copy(w3.signatureR.begin(), w3.signatureR.end(), signature.begin());
    std::copy(w3.signatureS.begin(), w3.signatureS.end(), signature.begin() + 32);
    signature[64] = static_cast<bcos::byte>(w3.signatureV);
    {
        auto reassembled = bcostars::protocol::reassembleWeb3RawTransaction(
            bcos::bytesConstRef(signPayload.data(), signPayload.size()),
            bcos::bytesConstRef(signature.data(), signature.size()));
        BOOST_REQUIRE(bcos::engine::engine_common::op::opEnvelopeToTars(reassembled, bcos::h256{}));
    }

    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(signPayload.begin(), signPayload.end());
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    tx->forceSender(bcos::fromHex(w3.sender()));
    bcos::crypto::Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(static_cast<int64_t>(nonce));
    return DecodableWeb3Tx{.tx = std::move(tx), .rawHex = bcos::toHexStringWithPrefix(raw)};
}

struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };
    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }
    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

using EngineOpSchedulerBase = bcos::evm::engine::OpSchedulerSeam<ViewType>;
/// Production seam synthesizes from L1BlockInfo. Fixtures keep the zero envelope.
struct EngineOpScheduler : EngineOpSchedulerBase
{
    using EngineOpSchedulerBase::EngineOpSchedulerBase;
    [[nodiscard]] bcos::bytes synthesizeL1AttributesEnvelope() const
    {
        return bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(isJovianActive());
    }
};
using EthLegacyEngine =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, EngineOpScheduler>;
using OpEngine = bcos::engine::OpEngineService<StubMemPool, MLS, EngineOpScheduler>;

static_assert(bcos::engine::EngineServiceConcept<EthLegacyEngine>);
static_assert(bcos::engine::EngineServiceConcept<OpEngine>);

constexpr bcos::protocol::BlockNumber c_headOrderingBlockNumber = 40;
constexpr bcos::protocol::BlockNumber c_safeOrderingBlockNumber = 41;
constexpr bcos::protocol::BlockNumber c_finalizedOrderingBlockNumber = 42;

constexpr char const* c_opV4UnsupportedForkMessage =
    "Isthmus+ payloads require engine_newPayloadV4 (JSON-RPC -38005)";
constexpr char const* c_safeAboveHeadMessage =
    "Forkchoice safe block number must not exceed head block number";
constexpr char const* c_finalizedAboveHeadMessage =
    "Forkchoice finalized block number must not exceed head block number";
constexpr char const* c_finalizedAboveSafeMessage =
    "Forkchoice finalized block number must not exceed safe block number";

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

void registerVerifiedBlock(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::storage::Entry hashEntry;
    hashEntry.set(blockHash.asBytes());
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(hashEntry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

void registerHashToNumberOnly(MLS& multiLayerStorage, bcos::h256 const& blockHash, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

void registerCurrentBlockNumber(MLS& multiLayerStorage, int64_t number)
{
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

void registerParentHeader(MLS& multiLayerStorage, bcos::protocol::BlockFactory& blockFactory,
    int64_t number, int64_t timestampMs)
{
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(timestampMs);
    header->setGasLimit(30'000'000);
    header->setGasUsed(0);
    header->setExtraData(bcos::fromHex("00000000fa00000006"));
    header->setBaseFee(bcos::u256(1'000'000'000));
    bcos::bytes encoded;
    header->encode(encoded);
    auto view = multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(number)},
        std::move(entry)));
    bcos::task::syncWait(multiLayerStorage.mergeView(std::move(view)));
}

bcos::engine::NewPayloadRequest makeValidIsthmusNewPayload(
    bcos::protocol::BlockFactory& blockFactory, bcos::h256 const& parentHash,
    bcos::protocol::BlockNumber blockNumber)
{
    bcos::engine::NewPayloadRequest request;
    auto& payload = request.executionPayload;
    payload.parentHash = parentHash;
    payload.blockNumber = blockNumber;
    payload.timestamp = 1'700'000'000'000ULL;
    payload.gasLimit = 30'000'000;
    payload.gasUsed = 0;
    payload.baseFeePerGas = 1;
    payload.transactions = {};
    payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    payload.withdrawalsRoot = bcos::h256{};
    payload.excessBlobGas = bcos::u256(0);
    payload.blobGasUsed = bcos::u256(0);
    payload.extraData = bcos::fromHex("00000000fa00000006");
    request.parentBeaconBlockRoot = bcos::h256{};
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::op_detail::rawEnvelopes(payload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        blockFactory.blockHeaderFactory(), payload, txRoot, *request.parentBeaconBlockRoot);
    payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    return request;
}

void checkStatusParity(
    bcos::engine::PayloadStatus const& left, bcos::engine::PayloadStatus const& right)
{
    BOOST_CHECK_EQUAL(static_cast<int>(left.status), static_cast<int>(right.status));
    BOOST_CHECK(left.latestValidHash == right.latestValidHash);
    BOOST_CHECK(left.validationError == right.validationError);
}

template <typename Exception>
void checkBothExceptionMessages(auto&& leftAction, auto&& rightAction, char const* expectedMessage)
{
    BOOST_CHECK_EXCEPTION(leftAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
    BOOST_CHECK_EXCEPTION(rightAction(), Exception, [&](Exception const& e) {
        auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
        return comment != nullptr && *comment == expectedMessage;
    });
}

bcos::engine::PayloadAttributes makeOpPayloadAttributes()
{
    bcos::engine::PayloadAttributes attrs;
    // Whole-second milliseconds for Eth RLP timestamp validation.
    attrs.timestamp = 1'700'000'000'000ULL;
    attrs.prevRandao = bcos::h256(std::string(64, '2'));
    attrs.suggestedFeeRecipient = bcos::Address(std::string(40, '3'));
    attrs.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attrs.parentBeaconBlockRoot = bcos::h256(std::string(64, '4'));
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes(8, 0);
    attrs.minBaseFee = 0;
    attrs.noTxPool = true;
    return attrs;
}

struct OpServicePair
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler scheduler{bcos::evm::opstack::OpForkFlags{}, {}};
    bcos::scheduler::SchedulerInterface::Ptr delegate;
    OpEngine service;

    explicit OpServicePair(bool allowSynthesizedL1Attributes = false,
        bcos::scheduler::SchedulerInterface::Ptr delegateIn = nullptr)
      : delegate(std::move(delegateIn)),
        service(memPool, storage, scheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3), delegate, nullptr,
            allowSynthesizedL1Attributes)
    {}
};

struct SharedForkchoicePair
{
    BackendMemStorage legacyBackend{1};
    BackendMemStorage opBackend{1};
    CheckpointBackend legacyCheckpoint{legacyBackend};
    CheckpointBackend opCheckpoint{opBackend};
    MLS legacyStorage{legacyCheckpoint};
    MLS opStorage{opCheckpoint};
    StubMemPool legacyMemPool;
    StubMemPool opMemPool;
    StubExecutor legacyExecutor;
    StubExecutor opExecutor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler legacyScheduler{bcos::evm::opstack::OpForkFlags{}, {}};
    EngineOpScheduler opScheduler{bcos::evm::opstack::OpForkFlags{}, {}};
    EthLegacyEngine legacy;
    OpEngine op;

    SharedForkchoicePair()
      : legacy(legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory),
        op(opMemPool, opStorage, opScheduler, blockFactory, nullptr,
            bcos::engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3), nullptr)
    {}
};

}  // namespace op_engine_parity_test

BOOST_AUTO_TEST_SUITE(OpEngineServiceParityTest)

using namespace op_engine_parity_test;

BOOST_AUTO_TEST_CASE(op_capabilities_include_op_methods)
{
    // Matrix: S5
    OpServicePair pair;
    auto caps = bcos::task::syncWait(pair.service.exchangeCapabilities({}));
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_newPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV5") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_forkchoiceUpdatedV3") != caps.end());
}

BOOST_AUTO_TEST_CASE(op_v3_new_payload_throws_unsupported_fork)
{
    // Matrix: S5 — release carrier is transactions[i].raw (empty list here).
    OpServicePair pair;
    bcos::engine::NewPayloadRequest request;
    request.executionPayload.timestamp = 1000;
    request.executionPayload.blockNumber = 1;
    request.executionPayload.transactions = {};
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(pair.service.newPayload(request, 3)),
        bcos::engine::UnsupportedFork, [&](bcos::engine::UnsupportedFork const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == c_opV4UnsupportedForkMessage;
        });
}

BOOST_AUTO_TEST_CASE(op_missing_gas_limit_returns_invalid)
{
    // Matrix: S5
    OpServicePair pair;
    auto attrs = makeOpPayloadAttributes();
    attrs.gasLimit = std::nullopt;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_gas_limit_above_signed_max_is_invalid)
{
    // Matrix: A1 — FCU INVALID (not -32603), same message as newPayload, no payloadId.
    OpServicePair pair;
    auto attrs = makeOpPayloadAttributes();
    attrs.gasLimit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK_EQUAL(*result.payloadStatus.validationError,
        "gasLimit exceeds the maximum block gas limit (2^63-1)");
}

BOOST_AUTO_TEST_CASE(op_safe_above_head_matches_eth_legacy)
{
    // Matrix: S5 — shared forkchoice ordering vs EngineServiceImpl.
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        bcos::h256("0000000000000000000000000000000000000000000000000000000000000011")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_safeAboveHeadMessage);
}

BOOST_AUTO_TEST_CASE(op_finalized_above_head_matches_eth_legacy)
{
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_finalizedAboveHeadMessage);
}

BOOST_AUTO_TEST_CASE(op_finalized_above_safe_matches_eth_legacy)
{
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("1212121212121212121212121212121212121212121212121212121212121212"),
        bcos::h256("1313131313131313131313131313131313131313131313131313131313131313"),
        bcos::h256("1414141414141414141414141414141414141414141414141414141414141414")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_finalizedAboveSafeMessage);
}

BOOST_AUTO_TEST_CASE(op_fast_path_concurrent_with_build_publish)
{
    // Matrix: S5 / S7-adjacent — shared guard blocks exclusive publish.
    bcos::engine::EngineTracker tracker;
    std::unordered_map<bcos::engine::PayloadID, bcos::engine::OpPayloadArtifacts> artifacts;
    auto blockFactory = makeBlockFactory();

    bcos::h256 const targetHash(0x42);
    bcos::engine::PayloadID const targetPayloadId = "0xdeadbeef";
    constexpr bcos::protocol::BlockNumber kTargetNumber = 7;

    {
        auto guard = tracker.lockExclusive();
        auto entry = std::make_shared<bcos::engine::BuiltPayload>();
        entry->executionPayload.blockHash = targetHash;
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(kTargetNumber);
        (void)bcos::engine::op_detail::publishBuiltPayload(guard, artifacts, targetPayloadId,
            targetHash, entry, bcos::engine::OpPayloadArtifacts{.canonicalHeader = header});
    }

    bcos::protocol::BlockHeader::Ptr initialHeader;
    std::atomic<bool> writerFinished{false};
    std::exception_ptr writerError;

    std::latch writerReady{1};
    std::latch permission{1};
    std::latch committed{1};

    std::optional<std::thread> writer;
    {
        auto shared = tracker.lockShared();
        initialHeader = bcos::engine::op_detail::findBuiltHeader(shared, artifacts, targetHash);
        BOOST_REQUIRE(initialHeader);
        BOOST_CHECK_EQUAL(initialHeader->number(), kTargetNumber);

        writer.emplace([&] {
            try
            {
                writerReady.count_down();
                permission.wait();
                committed.count_down();
                auto guard = tracker.lockExclusive();
                bcos::h256 writerHash(0x99);
                bcos::engine::PayloadID writerPayloadId = "0xcafebabe";
                auto entry = std::make_shared<bcos::engine::BuiltPayload>();
                entry->executionPayload.blockHash = writerHash;
                auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
                header->setNumber(99);
                (void)bcos::engine::op_detail::publishBuiltPayload(guard, artifacts,
                    writerPayloadId, writerHash, entry,
                    bcos::engine::OpPayloadArtifacts{.canonicalHeader = header});
                writerFinished.store(true, std::memory_order_release);
            }
            catch (...)
            {
                writerError = std::current_exception();
            }
        });

        writerReady.wait();
        permission.count_down();
        committed.wait();
        BOOST_CHECK(!writerFinished.load(std::memory_order_acquire));
    }

    writer->join();
    if (writerError)
    {
        std::rethrow_exception(writerError);
    }
    BOOST_CHECK(writerFinished.load(std::memory_order_acquire));

    {
        auto shared = tracker.lockShared();
        auto stableHeader = bcos::engine::op_detail::findBuiltHeader(shared, artifacts, targetHash);
        BOOST_REQUIRE(stableHeader);
        BOOST_CHECK_EQUAL(stableHeader->number(), kTargetNumber);
        BOOST_CHECK_EQUAL(stableHeader.get(), initialHeader.get());
    }
}

BOOST_AUTO_TEST_CASE(op_fcu_rejects_non_canonical_safe)
{
    // op-geth: HASH_2_NUMBER finds the block, but ReadCanonicalHash(number) differs.
    OpServicePair pair;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    auto const canonicalSafe =
        bcos::h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 10);
    registerVerifiedBlock(pair.storage, canonicalSafe, 8);
    registerHashToNumberOnly(pair.storage, forkchoice.safeBlockHash, 8);
    registerVerifiedBlock(pair.storage, forkchoice.finalizedBlockHash, 7);
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState, [&](bcos::engine::InvalidForkchoiceState const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == "Forkchoice safe block not in canonical chain";
        });
}

BOOST_AUTO_TEST_CASE(op_fcu_rejects_empty_txs_when_synthesis_disabled)
{
    // op_engine_rpc / op-geth: do not invent an L1-attributes deposit.
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::nullopt;
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(
        result.payloadStatus.validationError->find("L1 attributes deposit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_v4_is_unsupported)
{
    // Matrix: T05 — maxEngineVersion is V3, so FCU V4 is UnsupportedEngineApiVersion.
    OpServicePair pair;
    bcos::engine::ForkchoiceState state;
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.updateForkchoice(state, nullptr, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
}

BOOST_AUTO_TEST_CASE(op_culprit_hash_is_structured_not_message_text)
{
    // Matrix: T02 T03 — engine reads OpCulpritTxHash; a [tx=0x...] message is ignored.
    bcos::h256 const hash(std::string(64, 'a'));
    auto error = BCOS_ERROR_PTR(-1, "Execute block failed! invalid tx");
    *error << bcos::engine::OpCulpritTxHash(hash);
    auto got = bcos::engine::culpritTxHashFromError(*error);
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->hex(), hash.hex());

    auto plain = BCOS_ERROR_PTR(-1, "Execute block failed! [tx=0x" + hash.hex() + "]");
    BOOST_CHECK(!bcos::engine::culpritTxHashFromError(*plain).has_value());

    bcos::evm::OpConsensusError thrown("op block: invalid non-deposit tx");
    thrown.txHash = hash;
    auto attached = BCOS_ERROR_UNIQUE_PTR(1, thrown.what());
    *attached << bcos::engine::OpCulpritTxHash(*thrown.txHash);
    auto recovered = bcos::engine::culpritTxHashFromError(*attached);
    BOOST_REQUIRE(recovered.has_value());
    BOOST_CHECK_EQUAL(recovered->hex(), hash.hex());

    StubMemPool pool;
    std::array<bcos::crypto::HashType, 1> hashes{*recovered};
    pool.removeByHash(std::span<bcos::crypto::HashType const>(hashes));
    BOOST_REQUIRE_EQUAL(pool.removed.size(), 1);
    BOOST_CHECK_EQUAL(pool.removed.front().hex(), hash.hex());
}

BOOST_AUTO_TEST_CASE(op_newpayload_missing_parent_header_is_invalid)
{
    // BF — parent hash is canonical; SYS_NUMBER_2_BLOCK_HEADER is absent.
    OpServicePair pair;
    auto const parent =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    registerVerifiedBlock(pair.storage, parent, 0);
    auto request = makeValidIsthmusNewPayload(*pair.blockFactory, parent, 1);
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), parent.hex());
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK(status.validationError->find("parent") != std::string::npos);
    BOOST_CHECK(status.validationError->find("header") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_missing_parent_header_is_invalid)
{
    // BF — FCU build must not default baseFee to 1 gwei when the parent header is gone.
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("parent") != std::string::npos);
    BOOST_CHECK(result.payloadStatus.validationError->find("header") != std::string::npos);
}

static void driveBuildWithCulprit(bool capacityReject)
{
    auto delegate = std::make_shared<RecordingScheduler>();
    delegate->rejectAsCapacity = capacityReject;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    delegate->culprit = decoded.tx->hash();
    pair.memPool.pool.push_back(decoded.tx);

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.noTxPool = false;
    // Non-empty forced list skips L1-attributes synthesis; the envelope must still
    // decode in buildOpBlock so the retry loop is reachable.
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_GE(delegate->executeCalls, 2);
    if (capacityReject)
    {
        BOOST_CHECK(pair.memPool.removed.empty());
    }
    else
    {
        BOOST_REQUIRE_EQUAL(pair.memPool.removed.size(), 1);
        BOOST_CHECK_EQUAL(pair.memPool.removed.front().hex(), decoded.tx->hash().hex());
    }
}

BOOST_AUTO_TEST_CASE(op_build_capacity_reject_does_not_evict)
{
    // BH — OpRejectIsCapacity skips this candidate, the tx stays in the pool.
    driveBuildWithCulprit(/*capacityReject=*/true);
}

BOOST_AUTO_TEST_CASE(op_build_culprit_hash_evicts_through_retry_loop)
{
    // BC — structured OpCulpritTxHash on a sealed pool tx is evicted via removeByHash.
    driveBuildWithCulprit(/*capacityReject=*/false);
}

static void driveBuildWithSenderNonceChain(bool capacityReject)
{
    // R3-F1 / R2-F4 — same sender, nonce n then n+1. Excluding n must not evict n+1.
    auto delegate = std::make_shared<NonceChainScheduler>();
    delegate->rejectAsCapacity = capacityReject;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    bcos::crypto::Secp256k1Crypto secp;
    auto keyPair = secp.generateKeyPair();
    auto dummy = makeDecodableWeb3Tx(0);
    auto nonceN = makeDecodableWeb3Tx(1, keyPair.get());
    auto nonceN1 = makeDecodableWeb3Tx(2, keyPair.get());
    delegate->culprit = nonceN.tx->hash();
    delegate->successor = nonceN1.tx->hash();
    delegate->culpritEnvHash = envelopeHashOf(nonceN.tx);
    delegate->successorEnvHash = envelopeHashOf(nonceN1.tx);
    pair.memPool.pool.push_back(nonceN.tx);
    pair.memPool.pool.push_back(nonceN1.tx);

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.noTxPool = false;
    attrs.transactions = std::vector<std::string>{dummy.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_GE(delegate->executeCalls, 2);
    auto const n1Hex = nonceN1.tx->hash().hex();
    if (capacityReject)
    {
        BOOST_CHECK(pair.memPool.removed.empty());
    }
    else
    {
        BOOST_REQUIRE_EQUAL(pair.memPool.removed.size(), 1);
        BOOST_CHECK_EQUAL(pair.memPool.removed.front().hex(), nonceN.tx->hash().hex());
    }
    BOOST_CHECK(std::none_of(pair.memPool.removed.begin(), pair.memPool.removed.end(),
        [&](auto const& removed) { return removed.hex() == n1Hex; }));
}

BOOST_AUTO_TEST_CASE(op_build_capacity_skip_keeps_sender_successor)
{
    driveBuildWithSenderNonceChain(/*capacityReject=*/true);
}

BOOST_AUTO_TEST_CASE(op_build_intrinsic_evict_keeps_sender_successor)
{
    driveBuildWithSenderNonceChain(/*capacityReject=*/false);
}

BOOST_AUTO_TEST_CASE(op_fcu_unknown_nonzero_safe_is_invalid_forkchoice)
{
    // BJ — zero hash stays unset; a non-zero unresolved safe is InvalidForkchoiceState.
    OpServicePair pair;
    auto const head =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto const unknownSafe =
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    bcos::engine::ForkchoiceState forkchoice{head, unknownSafe, {}};
    registerVerifiedBlock(pair.storage, head, 0);
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState, [](bcos::engine::InvalidForkchoiceState const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && comment->find("unknown") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(op_fcu_undecodable_envelope_is_invalid_not_internal_error)
{
    // AM — FCU build must answer INVALID, not throw OpExecutionInternalError (-32603).
    auto delegate = std::make_shared<RecordingScheduler>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{"0xdeadbeef"};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("undecodable") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_getpayload_newpayload_roundtrip)
{
    // AR — service-level FCU → getPayload → newPayload with a real delegate.
    auto delegate = std::make_shared<RecordingScheduler>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());

    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 3));
    BOOST_REQUIRE(payload);
    BOOST_REQUIRE_EQUAL(bcos::toHex(payload->executionPayload.extraData), "00000000fa00000006");

    bcos::engine::NewPayloadRequest request;
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(pair.service.lastExecutedHeader());
}

BOOST_AUTO_TEST_CASE(op_newpayload_occupied_nontip_height_is_syncing)
{
    // Matrix: A2 — height N already has hash A, payload is hash B, tip is past N.
    // Engine API answers SYNCING; must not throw OpExecutionInternalError (-32603).
    OpServicePair pair;
    auto const parent =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto const occupied =
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auto const tip = bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    registerVerifiedBlock(pair.storage, parent, 0);
    registerVerifiedBlock(pair.storage, occupied, 1);
    registerVerifiedBlock(pair.storage, tip, 2);
    registerCurrentBlockNumber(pair.storage, 2);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto parentHeader = pair.blockFactory->blockHeaderFactory()->createBlockHeader();
    parentHeader->setNumber(0);
    parentHeader->setTimestamp(1'699'000'000'000);
    parentHeader->setGasLimit(30'000'000);
    parentHeader->setGasUsed(0);
    parentHeader->setExtraData(bcos::fromHex("00000000fa00000006"));
    parentHeader->setBaseFee(bcos::u256(1'000'000'000));

    auto request = makeValidIsthmusNewPayload(*pair.blockFactory, parent, 1);
    request.executionPayload.baseFeePerGas = bcos::engine::calcOpBaseFee(*parentHeader, false);
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::op_detail::rawEnvelopes(request.executionPayload));
    auto header =
        bcos::engine::engine_common::op::rebuildOpEthHeader(pair.blockFactory->blockHeaderFactory(),
            request.executionPayload, txRoot, *request.parentBeaconBlockRoot);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    BOOST_CHECK_NE(request.executionPayload.blockHash.hex(), occupied.hex());

    bcos::engine::PayloadStatus status;
    BOOST_CHECK_NO_THROW(status = bcos::task::syncWait(pair.service.newPayload(request, 4)));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
    BOOST_CHECK(!status.latestValidHash.has_value());
    BOOST_CHECK(!status.validationError.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
