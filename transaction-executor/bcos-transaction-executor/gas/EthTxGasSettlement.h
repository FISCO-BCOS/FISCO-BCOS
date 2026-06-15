/*
 * Ethereum transaction gas settlement (EIP-7623 + intrinsic gas), transaction-executor only.
 *
 * Task-0-FROZEN: Branch A. executionBurn = gasBeforeEvm - gas_left.
 * effectiveRefund = min(gas_refund, gasUsedBeforeRefund / 5).
 */
#pragma once

#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/Features.h"
#include <evmc/evmc.h>
#include <algorithm>

namespace bcos::executor_v1
{
namespace gas
{

constexpr int64_t TX_BASE_GAS = 21000;
constexpr int64_t CREATE_BASE_GAS = 32000;
constexpr int64_t INITCODE_WORD_GAS = 2;
constexpr int64_t ACCESS_LIST_ADDRESS_COST = 2400;
constexpr int64_t ACCESS_LIST_STORAGE_KEY_COST = 1900;

struct TxIntrinsicGas
{
    int64_t txBase = TX_BASE_GAS;
    int64_t normalCalldata = 0;
    int64_t accessListCost = 0;
    int64_t createIntrinsic = 0;
    int64_t floorReserve = 0;

    int64_t fixedCost() const { return txBase + accessListCost; }

    int64_t preExecutionDebit() const { return fixedCost() + normalCalldata + createIntrinsic; }

    /// EIP-7623: max(21000 + floor, intrinsic) where intrinsic includes access list + CREATE.
    int64_t gasLimitMinimum() const
    {
        int64_t const intrinsic = preExecutionDebit();
        int64_t const floorTotal = txBase + floorReserve;
        return std::max(intrinsic, floorTotal);
    }
};

struct TxGasSettlementContext
{
    int64_t gasLimit = 0;
    int64_t gasBeforeEvm = 0;
    executor::Eip7623Components calldata{};
    int64_t fixedIntrinsic = 0;
    int64_t createTerm = 0;
    int64_t evmGasLeft = 0;
    int64_t evmGasRefund = 0;
};

inline bool eip7623Active(ledger::Features const& features, evmc_revision revision) noexcept
{
    return revision >= EVMC_PRAGUE && features.get(ledger::Features::Flag::feature_evm_prague);
}

inline bool ethGasSettlementEnabled(
    ledger::Features const& features, evmc_revision revision, int64_t level, bool web3Tx) noexcept
{
    return web3Tx && level == 0 && eip7623Active(features, revision);
}

inline bool eip7623TxpoolValidationEnabled(
    ledger::Features const& features, evmc_revision revision, bool web3Tx) noexcept
{
    return web3Tx && eip7623Active(features, revision);
}

inline int64_t calcAccessListCost(executor::Eip2930AccessList const* accessList) noexcept
{
    if (accessList == nullptr)
    {
        return 0;
    }
    int64_t cost = 0;
    for (auto const& entry : *accessList)
    {
        cost += ACCESS_LIST_ADDRESS_COST;
        cost += static_cast<int64_t>(entry.second.size()) * ACCESS_LIST_STORAGE_KEY_COST;
    }
    return cost;
}

inline int64_t calcCreateIntrinsic(evmc_message const& message) noexcept
{
    if (message.kind != EVMC_CREATE && message.kind != EVMC_CREATE2)
    {
        return 0;
    }
    auto const inputSize = static_cast<int64_t>(message.input_size);
    auto const words = (inputSize + 31) / 32;
    return CREATE_BASE_GAS + INITCODE_WORD_GAS * words;
}

inline TxIntrinsicGas computeTxIntrinsicGas(evmc_message const& message,
    executor::Eip2930AccessList const* accessList, uint8_t web3TypedTxKind)
{
    TxIntrinsicGas intrinsic;
    auto const components = executor::calcEip7623Components(
        bcos::bytesConstRef(message.input_data, message.input_size));
    intrinsic.normalCalldata = components.normalCost;
    intrinsic.floorReserve = components.floorCost;

    if (web3TypedTxKind != 0 && accessList != nullptr)
    {
        intrinsic.accessListCost = calcAccessListCost(accessList);
    }

    intrinsic.createIntrinsic = calcCreateIntrinsic(message);
    return intrinsic;
}

inline int64_t effectiveRefundEip3529(int64_t evmGasRefund, int64_t gasUsedBeforeRefund) noexcept
{
    if (gasUsedBeforeRefund <= 0)
    {
        return 0;
    }
    return std::min(evmGasRefund, gasUsedBeforeRefund / 5);
}

inline int64_t finalizeEthereumGasUsed(TxGasSettlementContext const& ctx) noexcept
{
    int64_t const executionBurn = ctx.gasBeforeEvm - ctx.evmGasLeft;
    // CREATE intrinsic is debited inside evmone from gasBeforeEvm when execution runs; when
    // executionBurn already covers createTerm, do not add it again (avoids double-count).
    int64_t const createExtra =
        (ctx.createTerm > 0 && executionBurn < ctx.createTerm) ? ctx.createTerm - executionBurn : 0;
    // Snapshot is taken after normal calldata pre-debit and 21000 base; executionBurn is EVM-only.
    int64_t const gasUsedBeforeRefund =
        ctx.fixedIntrinsic + ctx.calldata.normalCost + executionBurn + createExtra;
    int64_t const effectiveRefund = effectiveRefundEip3529(ctx.evmGasRefund, gasUsedBeforeRefund);
    // geth (Prague+): after refund, top up only when total used is below FloorDataGas
    // (21000 + floor tokens; access list excluded from floor).
    int64_t const gasUsedAfterRefund = gasUsedBeforeRefund - effectiveRefund;
    int64_t const floorDataGas = TX_BASE_GAS + ctx.calldata.floorCost;
    return std::max(gasUsedAfterRefund, floorDataGas);
}

}  // namespace gas
}  // namespace bcos::executor_v1
