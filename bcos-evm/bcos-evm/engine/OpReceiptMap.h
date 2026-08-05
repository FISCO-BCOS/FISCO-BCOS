// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpReceiptMap — OP block-execution receipt (evmone::state::TransactionReceipt) ->
// bcos::protocol::TransactionReceipt (op-validator-minimal-loop design §4.1/§4.3,
// task-4-brief.md). Scope is deliberately narrow, per brief: only status/gasUsed/logs are
// mapped. contractAddress/output/blockNumber are not part of the six-way comparison surface
// (design §4.1 "六项比对面": seal.{receiptsRoot,logsBloom,withdrawalsRoot} +
// result.{stateRoot,gasUsed,txRoot}) and are left at their factory defaults (empty
// contractAddress, empty output, blockNumber 0) — a genuine widening of this mapping (OP meta
// fields such as deposit_nonce/l1_fee/operator_fee/da_footprint, per OpReceiptMeta.h) is
// out-of-scope non-goal territory per the design doc §2 table ("OP meta 回执 RPC").
//
// receiptFactory is injected (constructor parameter on OpSchedulerImpl, design §4.1 "receipt
// factory 注入"), not looked up globally — this header is a pure function, no state.
//
// Status mapping (FISCO convention, e.g. precompiled contracts / BlockExecutive.cpp receipt
// construction): 0 == success. evmc_status_code's much finer-grained failure taxonomy collapses
// to a single non-zero "failed" code here — this is the "只映射 status" scope the brief
// specifies, not an oversight.

#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstring>
#include <intx/intx.hpp>

namespace bcos::evm::engine
{

/// FISCO convention: TransactionStatus::None (success) == 0. Any non-EVMC_SUCCESS status
/// collapses to 1 (generic failure) — see file header "Status mapping" note.
inline constexpr int32_t kOpReceiptStatusSuccess = 0;
inline constexpr int32_t kOpReceiptStatusFailure = 1;

/// bytes-for-bytes copy of a 20-byte address into a bcos::bytes payload — LogEntry::address()
/// reinterprets its stored bytes as raw binary (not a hex string), see LogEntry.h:41, so this is
/// a plain byte copy, not a hex encode.
inline bcos::bytes mapOpLogAddress(const evmc::address& addr)
{
    return bcos::bytes(std::begin(addr.bytes), std::end(addr.bytes));
}

inline bcos::h256 mapOpTopic(const evmc::bytes32& topic)
{
    return bcos::h256(reinterpret_cast<const bcos::byte*>(topic.bytes), sizeof(topic.bytes));
}

inline bcos::protocol::LogEntries mapOpLogs(const std::vector<evmone::state::Log>& logs)
{
    bcos::protocol::LogEntries out;
    out.reserve(logs.size());
    for (const auto& log : logs)
    {
        bcos::h256s topics;
        topics.reserve(log.topics.size());
        for (const auto& topic : log.topics)
            topics.push_back(mapOpTopic(topic));
        out.emplace_back(mapOpLogAddress(log.addr), std::move(topics),
            bcos::bytes(log.data.begin(), log.data.end()));
    }
    return out;
}

/// Local helper: intx::uint256 → bcos::u256, full-width big-endian conversion. intx only exposes
/// an explicit low-64-bit cast operator, so a plain `static_cast<bcos::u256>` would silently drop
/// the high 192 bits — go through a big-endian byte store + bcos::fromBigEndian instead (same
/// pattern as ReceiptResponse.cpp's decode path).
inline bcos::u256 intxToBcosU256(intx::uint256 const& val)
{
    auto be = intx::be::store<evmc::uint256be>(val);
    return bcos::fromBigEndian<bcos::u256>(
        bcos::bytesConstRef{reinterpret_cast<bcos::byte const*>(be.bytes), sizeof(be.bytes)});
}

/// Convert the execution layer's opstack::OpReceiptMeta into the framework layer's
/// bcos::protocol::OpStackReceiptMeta (the typed view over the tars opStackMeta hex-string
/// fields). uint256 fields use intxToBcosU256 (full-width); uint64/uint32 scalar fields are
/// assigned directly. Presence is preserved per-field.
inline bcos::protocol::OpStackReceiptMeta toOpStackMeta(
    const bcos::evm::opstack::OpReceiptMeta& meta)
{
    bcos::protocol::OpStackReceiptMeta out;
    if (meta.l1_gas_price)
        out.l1_gas_price = intxToBcosU256(*meta.l1_gas_price);
    if (meta.l1_fee)
        out.l1_fee = intxToBcosU256(*meta.l1_fee);
    if (meta.l1_blob_base_fee)
        out.l1_blob_base_fee = intxToBcosU256(*meta.l1_blob_base_fee);
    if (meta.l1_base_fee_scalar)
        out.l1_base_fee_scalar = *meta.l1_base_fee_scalar;  // uint64 标量直接赋值
    if (meta.l1_blob_base_fee_scalar)
        out.l1_blob_base_fee_scalar = *meta.l1_blob_base_fee_scalar;
    if (meta.operator_fee_scalar)
        out.operator_fee_scalar = *meta.operator_fee_scalar;
    if (meta.operator_fee_constant)
        out.operator_fee_constant = *meta.operator_fee_constant;
    if (meta.da_footprint_gas_scalar)
        out.da_footprint_gas_scalar = *meta.da_footprint_gas_scalar;
    if (meta.da_footprint)
        out.da_footprint = *meta.da_footprint;
    if (meta.l1_gas_used)
        out.l1_gas_used = *meta.l1_gas_used;
    if (meta.operator_fee)
        out.operator_fee = intxToBcosU256(*meta.operator_fee);
    return out;
}

/// Maps one OP-executed transaction's receipt (the evmone::state::TransactionReceipt common to
/// both OpDepositReceipt and OpTxReceipt, see OpBlockExecute.h's OpBlockResult) into a
/// bcos::protocol::TransactionReceipt via the injected factory. Only status/gasUsed/logs are
/// populated from the evmone receipt; blockNumber and the OP receipt meta (opStackMeta) are
/// carried explicitly — the meta is what lets the RPC layer emit op-geth's OP extension fields
/// (ReceiptResponse.cpp reads it straight off the struct; see EthEndpoint's OP fallback path).
inline bcos::protocol::TransactionReceipt::Ptr mapOpReceipt(
    const evmone::state::TransactionReceipt& receipt,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    protocol::BlockNumber blockNumber,
    std::optional<bcos::protocol::OpStackReceiptMeta> opStackMeta,
    std::optional<intx::uint256> effectiveGasPrice = std::nullopt)
{
    const bcos::u256 gasUsed(static_cast<uint64_t>(receipt.gas_used));
    const int32_t status =
        receipt.status == EVMC_SUCCESS ? kOpReceiptStatusSuccess : kOpReceiptStatusFailure;
    auto out = receiptFactory->createReceipt(gasUsed, std::string{}, mapOpLogs(receipt.logs),
        status, bcos::bytesConstRef{}, blockNumber);
    if (effectiveGasPrice)
    {
        // op-geth hexutil.Big: "0x" + lowercase hex, no leading zeros.
        out->setEffectiveGasPrice("0x" + intx::to_string(*effectiveGasPrice, 16));
    }
    if (opStackMeta.has_value())
    {
        out->setOpStackMeta(*opStackMeta);
    }
    return out;
}

}  // namespace bcos::evm::engine
