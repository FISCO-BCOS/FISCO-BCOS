#pragma once

// Production OP block-execution helper for tests: preBlockOpSteps →
// SchedulerSerialImpl(serial=true) → finalizeOpBlockResult. Replaces the retired
// processOpBlock injection loop as the t8n / injector / shape-test driver.

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <engine/bcos-engine/OpEngineService.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpstackExecutor.h>
#include <range/v3/view/transform.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace opstack_test
{

using MutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;

inline bcos::protocol::Transaction::Ptr buildFiscoTxFromEnvelope(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto txHash = hashImpl->hash(env);
    auto tarsTx =
        bcos::engine::engine_common::op::opEnvelopeToTars(env, txHash, /*allowDeposit=*/true);
    if (!tarsTx)
        return nullptr;
    tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsBody = std::move(*tarsTx)]() mutable { return &tarsBody; });
}

/// Header carrying every optional `toBlockInfo` reads via `.value()`. Timestamp is
/// milliseconds (FISCO convention); callers convert evmone/op-geth seconds with `* 1000`.
inline std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeMinimalHeader(int64_t number,
    int64_t timestampMillis, int64_t gasLimit, bcos::u256 baseFee, bcos::Address coinbase,
    bcos::h256 prevRandao, bcos::h256 parentBeacon, bcos::h256 parentHash)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = (number > 0 ? number - 1 : 0), .blockHash = parentHash});
    h->setCoinbase(std::move(coinbase));
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(gasLimit));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(prevRandao);
    h->setBaseFee(std::move(baseFee));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(parentBeacon);
    h->setRequestsHash(bcos::h256{});
    return h;
}

inline bcos::evm::engine::OpExecuteBlockResult runSharedPath(MutableStorage& storage,
    bcos::protocol::BlockHeader const& header, std::vector<bcos::bytes> const& rawTxBytes,
    std::vector<bcos::protocol::Transaction::ConstPtr> const& transactions,
    std::vector<bcos::evm::opstack::DepositTx> const& deposits,
    bcos::evm::opstack::OpForkConfig const& cfg,
    bcos::executor_v1::opstack::OpstackExecutor& executor, uint64_t chainId,
    bcos::IOServicePool::Ptr const& ioServicePool)
{
    namespace detail = bcos::evm::engine::detail;
    bcos::ledger::LedgerConfig execLedgerConfig;
    execLedgerConfig.setEVMCRevision(cfg.rev);

    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<detail::RecentBlockHashes<MutableStorage>> hashes;
    static auto const schedule = bcos::evm::opstack::OpForkSchedule::legacy(false);
    bcos::evm::engine::preBlockOpSteps(storage, header, cfg, rawTxBytes, deposits, executor, hashes,
        hashErr, daFootprintGasScalar, &schedule, /*parentTsSec=*/0);
    bcos::executor_v1::opstack::OpBlockExecutionContext ctx{.fee = {},
        .blockGasLeft = static_cast<int64_t>(
            detail::narrowU256ToU64(header.gasLimit(), "runSharedPath blockGasLeft")),
        .blockHashes = &*hashes,
        .chainId = chainId,
        .daFootprintGasScalar = daFootprintGasScalar};
    bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
        ioServicePool, /*chunkSize=*/1, /*serial=*/true);
    auto transactionsRefs =
        transactions |
        ::ranges::views::transform([](bcos::protocol::Transaction::ConstPtr const& ptr)
                                       -> bcos::protocol::Transaction const& { return *ptr; });
    auto receipts = bcos::task::syncWait(serialScheduler.executeBlock(
        storage, executor, header, transactionsRefs, execLedgerConfig, ctx));
    return bcos::evm::engine::finalizeOpBlockResult(executor, storage, header, execLedgerConfig,
        cfg, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr);
}

}  // namespace opstack_test
