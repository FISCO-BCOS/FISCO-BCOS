/// @file OpEthL1Attributes.h
/// @brief L1-attributes deposit synthesis on the bcos-evm-free OP layer —
///        the counterpart of bcos-evm's OpTransition.h L1BlockInfo /
///        isUnsetL1BlockInfo / isUnsetSystemConfig and OpDepositEncode.h's
///        synthesizeL1AttributesDeposit / encodeDepositEnvelope, plus the
///        seam-level synthesizeL1AttributesEnvelope rule (OpSchedulerSeam.h).
///
/// Amounts are bcos::u256 (the ethereum-executor's native uint256) and the
/// deposit is the new-layer DepositTx (OpEthDeposit.h) — the encoder here is
/// its wire-format inverse of decodeOpDepositEnvelope, so encode/decode
/// round-trip by construction. Address constants come from OpFeeParams.h
/// (OP_DEPOSITOR / OP_L1_BLOCK, already byte-cross-checked against the
/// production OpStackConstants.h); the calldata length constants come from
/// OpEthBlockExecute.h (OP_ETH_*_L1_ATTRIBUTES_LEN).

#pragma once

#include <opstack-executor/OpEthBlockExecute.h>  // OP_ETH_*_L1_ATTRIBUTES_LEN / Jovian selector
#include <opstack-executor/OpEthDeposit.h>       // DepositTx / OP_DEPOSITOR (via OpFeeParams.h)
#include <opstack-executor/OpFeeParams.h>        // OP_DEPOSITOR / OP_L1_BLOCK
#include <opstack-executor/OpForkSpec.h>         // OpForkSchedule / opForkTimestampSec
#include <bcos-utilities/Common.h>
#include <array>
#include <cstdint>
#include <evmc/evmc.hpp>

namespace bcos::executor_v1::opstack
{
/// L1-attributes calldata selectors (op-node L1BlockInfo marshalBinary*):
/// Isthmus setL1BlockValuesIsthmus, Jovian setL1BlockValuesJovian. The Jovian
/// selector lives in OpEthBlockExecute.h (OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR)
/// next to the block-shape validation that consumes it.
inline constexpr std::array<uint8_t, 4> OP_ETH_ISTHMUS_L1_ATTRIBUTES_SELECTOR = {
    0x09, 0x89, 0x99, 0xbe};

/// Gas limit used when synthesizing the L1-attributes deposit.
inline constexpr int64_t OP_ETH_L1_INFO_DEPOSIT_GAS = 1'000'000;

/// L1 block fields for synthesizing the L1-attributes deposit (the
/// bcos::u256 counterpart of bcos-evm's L1BlockInfo).
/// All-zero number/time/blockHash is the unset snapshot sentinel.
/// All-zero baseFeeScalar or batcherHash means SystemConfig was not supplied.
struct OpEthL1BlockInfo
{
    uint64_t number = 0;
    uint64_t time = 0;
    bcos::u256 baseFee{0};
    evmc::bytes32 blockHash{};
    uint64_t sequenceNumber = 0;
    bcos::u256 blobBaseFee{0};
    uint32_t baseFeeScalar = 0;
    uint32_t blobBaseFeeScalar = 0;
    evmc::bytes32 batcherHash{};
    uint32_t operatorFeeScalar = 0;
    uint64_t operatorFeeConstant = 0;
};

[[nodiscard]] inline bool isUnsetOpEthL1BlockInfo(const OpEthL1BlockInfo& info) noexcept
{
    return info.number == 0 && info.time == 0 && evmc::is_zero(info.blockHash);
}

[[nodiscard]] inline bool isUnsetOpEthSystemConfig(const OpEthL1BlockInfo& info) noexcept
{
    return info.baseFeeScalar == 0 || evmc::is_zero(info.batcherHash);
}

/// 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data])
/// — the wire-format inverse of decodeOpDepositEnvelope (OpEthDeposit.h).
[[nodiscard]] bcos::bytes encodeOpEthDepositEnvelope(const DepositTx& dep);

/// The L1-attributes deposit (not yet envelope-encoded). sourceHash =
/// keccak256(bytes32(1) || keccak256(l1BlockHash || bytes32(seq))) — L2 time
/// is deliberately not bound into the sourceHash, so the builder takes no L2
/// time. Calldata is the setL1BlockValues* layout — Isthmus 176B, Jovian 178B
/// (op-node L1BlockInfo marshalBinaryIsthmus/Jovian); the Jovian DA-footprint
/// scalar [176:178] is zero (op-node decodes zero as its default).
[[nodiscard]] DepositTx buildOpEthL1AttributesDeposit(
    const OpEthL1BlockInfo& l1Info, bool jovianLayout);

/// The L1-attributes deposit envelope (0x7e). Does NOT apply the unset
/// sentinels — that refusal is the seam-level envelope function below (the
/// raw encoder stays callable for test fixtures, same as the legacy
/// synthesizeL1AttributesDeposit).
[[nodiscard]] bcos::bytes synthesizeOpEthL1AttributesDeposit(
    const OpEthL1BlockInfo& l1Info, bool jovianLayout);

/// Seam-level form (mirror of OpSchedulerSeam::synthesizeL1AttributesEnvelope):
/// refuses the unset snapshot sentinel (number/time/hash all zero) and an
/// unset SystemConfig (zero baseFeeScalar or batcherHash) so a missing CL
/// snapshot cannot mint a plausible L1-attributes deposit.
///
/// The calldata layout is keyed on the CHILD L2 block's timestamp (op-node
/// derive/l1_block_info.go L1InfoDeposit(..., l2Timestamp)) with ONE
/// exception: op-node gates it on `isJovianButNotFirstBlock`, i.e.
/// `IsJovian(ts) && !IsJovianActivationBlock(ts)` (l1_block_info.go:462-470),
/// so the ACTIVATION block itself still emits the previous fork's 176-byte
/// Isthmus layout — that is the block in which the L1Block predeploy is
/// upgraded, and it cannot already speak the new ABI.
/// `IsJovianActivationBlock(t)` is `IsJovian(t) && !IsJovian(t - blockTime)`,
/// and `t - blockTime` is exactly the parent's timestamp on an OP chain's
/// fixed cadence, so passing the parent lets this reproduce op-node's rule
/// without the schedule having to carry a block_time.
///
/// Throws std::invalid_argument on the unset sentinels.
[[nodiscard]] bcos::bytes synthesizeOpEthL1AttributesEnvelope(const OpForkSchedule& schedule,
    const OpEthL1BlockInfo& l1Info, int64_t l2InternalTimestampMs,
    int64_t parentInternalTimestampMs);
}  // namespace bcos::executor_v1::opstack
