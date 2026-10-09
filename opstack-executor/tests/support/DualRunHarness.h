#pragma once
// Shared harness for the bcos-evm-free OP executor tests. Holds the pieces
// OpEthExecutorDualRunTest.cpp (C1) and OpEthForkMatrixTest.cpp (C2) both drive:
//   - the MLS/storage fixture (TrivialCheckpointStorage + MemoryStorage aliases + DualRunFixture),
//   - envelope -> tars transaction carrier (buildFiscoTx),
//   - the production-scheduler driver (runExecutorPath): preBlockOpEthSteps ->
//     SchedulerSerialImpl(serial=true) over OpEthExecutor -> finalizeOpEthBlockResult
//     (OpEthBlockSteps.h) — the shape OpScheduler::execute and OpBlockVerifier::verifyAndCommit
//     run. The golden `_op_expected` block in support/GoldenExpect.h is the arbiter for C1;
//     C2 executes this same path twice from independent MLS forks as a determinism twin
//     (migration step 3.3 retired the legacy preBlockOpSteps/finalizeOpBlockResult baseline
//     leg, and the test-only monolithic executeOpEthBlock driver was later deleted with the
//     production cutover it duplicated).
//   - flat-row state diff diagnostics (collectRows / dumpDeltaRows / dumpRowDiff) for
//     stateRoot-mismatch bring-up.
// Suite-level plumbing (t8n corpus loading, golden headers) stays in support/GoldenSample.h.

#include <opstack-executor/OpEthBlockSteps.h>    // preBlockOpEthSteps / finalizeOpEthBlockResult
#include <opstack-executor/OpEthCommitments.h>   // OpEthExecuteBlockResult
#include <opstack-executor/OpEthDeposit.h>       // decodeOpDepositEnvelope
#include <opstack-executor/OpEthExecutor.h>      // OpEthExecutor / OpEthBlockContext
#include <opstack-executor/OpForkSpec.h>         // OpForkSpec

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Constants.h>   // emptyRootHash
#include <bcos-ledger/mpt/StateRoots.h>  // computeMptStateRoot
#include <bcos-rlp-protocol/Web3Transaction.h>  // decodeWeb3RawTransaction
#include <bcos-storage/KeyPrefixes.h>          // kMPTTable (row-diff filter)
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>  // makeMinimalHeader
#include <bcos-tars-protocol/protocol/TransactionImpl.h>  // mutableInner (buildFiscoTx)
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opstack_test
{
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace opeth = bcos::executor_v1::opstack;

// The FISCO OP chain id — the same constant OpSchedulerTest's kChainId pins and the t8n corpus
// envelopes embed (8453 = 0x2105).
inline constexpr uint64_t kOpChainId = 0x2105;

// Minimal CheckpointStorage stub (same shape as OpSchedulerTest's).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) & { std::abort(); }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
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

inline bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

inline bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

struct DualRunFixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
};

/// Minimal tars header carrying the fields the OP block path reads (number/ms timestamp/
/// gasLimit/baseFee/coinbase/prevRandao/parentBeaconBlockRoot + parentHash via ParentInfo).
/// Commitment fields (state/txs/receipts roots) stay zero — fillAnnouncedHeaderFromGolden
/// backfills them from the vector where a golden compare runs.
/// Fork-optional fields follow the fork's header shape, never stamped unconditionally:
/// Canyon+ withdrawalsRoot, Ecotone+ blob pair + parentBeaconBlockRoot, Isthmus+
/// requestsHash — an unconditional stamp would leak the field into a pre-fork header and
/// fail the six-way presence compare (regolith_* vectors caught exactly this).
inline std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeMinimalHeader(
    bcos::protocol::BlockNumber number, int64_t timestampMillis, int64_t gasLimit,
    bcos::u256 baseFee, bcos::Address coinbase, bcos::h256 prevRandao,
    bcos::h256 parentBeaconBlockRoot, bcos::h256 parentHash, opeth::OpForkSpec const& spec)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = parentHash});
    h->setCoinbase(std::move(coinbase));
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(gasLimit));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(prevRandao);
    h->setBaseFee(std::move(baseFee));
    if (spec.has_withdrawals)
    {
        h->setWithdrawalsRoot(bcos::h256{});
    }
    if (spec.fork >= bcos::ledger::OpFork::Ecotone)
    {
        h->setBlobGasUsed(bcos::u256(0));
        h->setExcessBlobGas(bcos::u256(0));
        h->setParentBeaconBlockRoot(parentBeaconBlockRoot);
    }
    if (spec.fork >= bcos::ledger::OpFork::Isthmus)
    {
        h->setRequestsHash(bcos::h256{});
    }
    return h;
}

/// Envelope → executable tars transaction, mirroring the engine's opEnvelopeToTars +
/// decodedTransactionFromEnvelope carrier step: decodeWeb3RawTransaction fills the tars mirror
/// from the decoded envelope and recovers/assigns the sender (deposit senders come from the
/// 0x7e `from` field). The raw wire bytes then OVERWRITE extraTransactionBytes —
/// takeToTarsTransaction stores the signing preimage there for non-deposit types, but the
/// executor must see the exact wire form — and extraTransactionHash is pinned to
/// keccak(envelope).
inline bcos::protocol::Transaction::Ptr buildFiscoTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto tx = bcos::rpc::decodeWeb3RawTransaction(
        bcos::bytesConstRef{env.data(), env.size()}, *hashImpl);
    if (tx == nullptr)
    {
        throw std::runtime_error("buildFiscoTx: envelope decode failed");
    }
    auto const txHash = hashImpl->hash(env);
    tx->mutableInner().extraTransactionBytes.assign(env.begin(), env.end());
    tx->mutableInner().extraTransactionHash.assign(txHash.begin(), txHash.end());
    return tx;
}

/// Envelope → tars transaction for the suites that hold the pre-cutover helper name.
/// buildFiscoTxFromEnvelope is the same envelope bridge buildFiscoTx provides; the alias
/// exists so the ported suites (OpT8nReplayTest, OpBlockInjectorTest) call one shared
/// definition instead of each keeping a private copy.
inline bcos::protocol::Transaction::Ptr buildFiscoTxFromEnvelope(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    return buildFiscoTx(env, hashImpl);
}


/// Production-scheduler driver (the shape OpScheduler::execute and
/// OpBlockVerifier::verifyAndCommit run): preBlockOpEthSteps ->
/// SchedulerSerialImpl(serial=true) over OpEthExecutor -> finalizeOpEthBlockResult with the
/// full state-root rebuild (skipStateRootBuild=false — the tests sit on a genesis-less seeded
/// state, so there is no base trie to increment from).
template <class ViewType>
opeth::OpEthExecuteBlockResult runExecutorPath(DualRunFixture& f, ViewType& view,
    bcos::protocol::BlockHeader const& header, opeth::OpForkSpec const& spec,
    std::vector<bcos::protocol::Transaction::ConstPtr> const& transactions,
    std::vector<bcos::bytes> const& rawTxBytes)
{
    std::vector<opeth::DepositTx> deposits;
    deposits.reserve(rawTxBytes.size());
    for (auto const& env : rawTxBytes)
        if (!env.empty() && env[0] == opeth::OP_DEPOSIT_TX_TYPE)
            deposits.push_back(
                opeth::decodeOpDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()}));

    auto sharedError = std::make_shared<opeth::OpStorageErrorSlot>();
    opeth::OpEthExecutor executor{f.receiptFactory, spec, sharedError};

    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<opeth::OpRecentBlockHashes<ViewType>> hashes;
    bcos::task::syncWait(opeth::preBlockOpEthSteps(view, header, spec, rawTxBytes, deposits,
        executor.vm(), sharedError, hashes, hashErr, daFootprintGasScalar));

    opeth::OpEthBlockContext ctx{.fee = {},
        .blockGasLeft = static_cast<int64_t>(header.gasLimit()),
        .blockHashLookup = opeth::opEthBlockHashLookup(*hashes),
        .chainId = kOpChainId,
        .daFootprintGasScalar = daFootprintGasScalar};

    bcos::ledger::LedgerConfig execLedgerConfig;
    execLedgerConfig.setEVMCRevision(spec.rev);

    bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
        f.ioServicePool, /*chunkSize=*/1, /*serial=*/true);
    auto transactionsRefs =
        transactions |
        ::ranges::views::transform([](bcos::protocol::Transaction::ConstPtr const& ptr)
                                       -> bcos::protocol::Transaction const& { return *ptr; });
    auto receipts = bcos::task::syncWait(serialScheduler.executeBlock(
        view, executor, header, transactionsRefs, execLedgerConfig, ctx));
    return bcos::task::syncWait(
        opeth::finalizeOpEthBlockResult(view, header, execLedgerConfig, spec, sharedError,
            std::move(receipts), rawTxBytes, ctx.cumulativeGasUsed, hashErr,
            /*skipStateRootBuild=*/false));
}

// ---- stateRoot-mismatch diagnostics: flat-row diff of two executed views ──────────────────

using RowMap = std::map<std::pair<std::string, std::string>, std::string>;

template <class ViewType>
bcos::task::Task<RowMap> collectRows(ViewType& view)
{
    RowMap rows;
    auto it = co_await bcos::storage2::range(view);
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        bcos::executor_v1::StateKeyView keyView(k);
        auto const& [table, key] = keyView.get();
        if (table == bcos::storage2::kMPTTable)
            continue;  // trie-node plane: present only on the new path's view, not flat state
        std::string value;
        if (auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(v)))
            value = bcos::toHex(entry->get());
        else
            value = "<deleted>";
        rows.emplace(
            std::pair{std::string(table), std::string(key)}, std::move(value));
    }
    co_return rows;
}

template <class ViewType>
bcos::task::Task<void> dumpDeltaRows(ViewType& view, char const* tag)
{
    auto it = co_await bcos::storage2::range(mutableStorage(view));  // ADL: MultiLayerStorage View friend
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        bcos::executor_v1::StateKeyView keyView(k);
        auto const& [table, key] = keyView.get();
        if (table == bcos::storage2::kMPTTable)
            continue;
        std::string value;
        if (auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(v)))
            value = bcos::toHex(entry->get());
        else
            value = "<deleted>";
        BOOST_TEST_MESSAGE("[delta " << tag << "] table=" << table << " key=" << bcos::toHex(key)
                                     << " value=" << value);
    }
    co_return;
}

template <class ViewType>
void dumpRowDiff(ViewType& viewA, ViewType& viewB)
{
    bcos::task::syncWait(dumpDeltaRows(viewA, "old"));
    bcos::task::syncWait(dumpDeltaRows(viewB, "new"));
    auto rowsA = bcos::task::syncWait(collectRows(viewA));
    auto rowsB = bcos::task::syncWait(collectRows(viewB));
    int shown = 0;
    auto show = [&](char side, std::pair<std::string, std::string> const& key,
                    std::string const& a, std::string const& b) {
        if (shown++ >= 60)
            return;
        BOOST_TEST_MESSAGE("[" << side << "] table=" << key.first << " key=" << bcos::toHex(key.second)
                               << " old=" << a << " new=" << b);
    };
    for (auto const& [key, va] : rowsA)
    {
        auto it = rowsB.find(key);
        if (it == rowsB.end())
            show('-', key, va, "<absent>");
        else if (it->second != va)
            show('!', key, va, it->second);
    }
    for (auto const& [key, vb] : rowsB)
        if (!rowsA.contains(key))
            show('+', key, "<absent>", vb);
    BOOST_TEST_MESSAGE("row diff: oldRows=" << rowsA.size() << " newRows=" << rowsB.size());
    for (auto const& [key, va] : rowsA)
    {
        auto it = rowsB.find(key);
        BOOST_TEST_MESSAGE("[old] table=" << key.first << " key=" << bcos::toHex(key.second)
                                          << " old=" << va
                                          << " new=" << (it == rowsB.end() ? "<absent>" : it->second));
    }
}

}  // namespace opstack_test
