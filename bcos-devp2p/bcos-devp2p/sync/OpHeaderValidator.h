/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpHeaderValidator.h
 * @brief OP Stack (Optimism L2) header validation for the eth header-sync path, ported
 *        from op-geth (optimism branch):
 *          - consensus/beacon/consensus.go verifyHeader (PoS constants, timestamp rule,
 *            extraData size, fork-gated withdrawalsHash / Cancun field presence)
 *          - consensus/misc/eip1559/eip1559.go VerifyEIP1559Header / CalcBaseFee
 *            (baseFee recomputation; OP gasLimit exempt from the 1/1024 bound)
 *          - consensus/misc/eip1559/eip1559_optimism.go ValidateOptimismExtraData
 *            (Holocene 9-byte / Jovian 17-byte extraData, empty before Holocene)
 *          - consensus/misc/eip4844/eip4844.go VerifyEIP4844Header / CalcExcessBlobGas
 *            (OP chains short-circuit excessBlobGas to 0)
 *        Fork activation resolves through bcos::ledger::resolveOpFork
 *        (bcos-framework/ledger/OpForkSchedule.h) — the single ladder parser the
 *        executor's configAt also delegates to, with op-node's rollup.json semantics
 *        (IsX(ts) == ts >= forkTime, unscheduled rungs implied by later forks,
 *        isthmus_time unset = Isthmus zero-start baseline).
 * @date 2026/9/21
 */
#pragma once

#include "HeaderValidator.h"
#include <bcos-framework/engine/Constants.h>
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <atomic>
#include <functional>
#include <limits>

namespace bcos::devp2p::sync
{
using bcos::ledger::OpFork;

/// Predeploy that conventionally receives the OP sequencer fees and shows up as the
/// header coinbase. op-geth does NOT check the coinbase at header level (it is fixed by
/// the payload attributes on the derivation path), so a mismatch is a warning here, not
/// a validation failure — see op-geth consensus/beacon/consensus.go verifyHeader.
inline const bcos::Address c_opSequencerFeeVault =
    bcos::Address("0x4200000000000000000000000000000000000011");

/// keccak256(rlp([])) of the empty withdrawals list — the withdrawalsHash every OP header
/// carries from Canyon until Isthmus (Isthmus+ replaces it with the L2ToL1MessagePasser
/// storage root, which cannot be checked at header level).
inline const bcos::h256 c_opEmptyWithdrawalsHash =
    bcos::h256("0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421");

/// sha256("") — the EIP-7685 empty requests hash every Isthmus+ OP header carries
/// (OP L2 blocks have no execution requests). Single-sourced in the framework.
inline const bcos::h256 c_opEmptyRequestsHash =
    bcos::h256(std::string(bcos::engine::c_emptyRequestsHashHex));

/// op-geth params.MaxGasLimit: every header's gasLimit must fit in a signed 63 bits.
constexpr bcos::u256 c_opMaxGasLimit{bcos::u256(std::numeric_limits<int64_t>::max())};

/// Minimal OP chain configuration for header validation. forkSchedule is the genesis
/// [op_fork_timestamps] schedule (bcos::ledger::OpForkSchedule); every fork gate below
/// resolves through bcos::ledger::resolveOpFork — the SAME ladder parser the executor
/// uses (bcos::evm::opstack::configAt delegates to it), so a schedule the executor
/// accepts can never be read differently here (an unset isthmus_time means "Isthmus is
/// the zero-start baseline", not "Isthmus inactive"; an unscheduled intermediate rung
/// is implied by a later scheduled fork).
struct OpChainConfig
{
    uint64_t chainId{10};
    uint64_t blockTimeSeconds{2};
    bcos::ledger::OpForkSchedule forkSchedule{};
    // op-geth params ChainConfig.Optimism constants (superchain-registry [optimism]
    // section; identical on every superchain chain: 50 / 250 / 6).
    uint64_t eip1559DenominatorBedrock{50};
    uint64_t eip1559DenominatorCanyon{250};
    uint64_t eip1559Elasticity{6};
};

namespace detail
{
/// baseFee — op-geth eip1559.VerifyEIP1559Header + CalcBaseFee: the expected value is
/// recomputed from the PARENT. Pre-Holocene parents use the chain-config constants, with
/// the denominator keyed on the CHILD's Canyon activation
/// (op-geth BaseFeeChangeDenominator(header.Time)); Holocene+ parents carry the EIP-1559
/// parameters in their own extraData (op-geth IsOptimismHolocene(parent.Time)); Jovian
/// parents additionally meter the DA footprint and apply the minBaseFee floor.
inline std::optional<std::string> validateOpBaseFee(
    bcos::protocol::EthBlockHeaderData const& _header,
    bcos::protocol::EthBlockHeaderData const& _parent, OpChainConfig const& _config)
{
    if (!_header.baseFee.has_value())
    {
        return "header is missing baseFee (OP chains are post-Bedrock/post-London)";
    }
    if (!_parent.baseFee.has_value())
    {
        return "parent header is missing baseFee";
    }
    bool const parentIsHolocene =
        bcos::ledger::resolveOpFork(_config.forkSchedule,
            static_cast<uint64_t>(_parent.timestamp)) >= OpFork::Holocene;
    bool const parentIsJovian =
        bcos::ledger::resolveOpFork(_config.forkSchedule,
            static_cast<uint64_t>(_parent.timestamp)) >= OpFork::Jovian;
    std::span<const bcos::byte> parentExtra{_parent.extraData.data(), _parent.extraData.size()};
    if (parentIsHolocene)
    {
        // The parent's extraData was shape-checked when the parent itself was validated
        // as a child (genesis is the only unvalidated ancestor); fail closed all the
        // same, matching op-geth's DecodeOptimismExtraData contract ("the parent
        // extraData is expected to be valid").
        if (auto err = bcos::engine::validateOpExtraDataShape(parentExtra, /*allowEmpty=*/false))
        {
            return "OP parent extraData " + *err;
        }
    }
    uint64_t const fallbackDenominator =
        bcos::ledger::resolveOpFork(_config.forkSchedule, static_cast<uint64_t>(_header.timestamp)) >=
                OpFork::Canyon ?
            _config.eip1559DenominatorCanyon :
            _config.eip1559DenominatorBedrock;
    try
    {
        auto const expected = bcos::engine::calcOpBaseFeeFromFields(_parent.gasLimit,
            _parent.gasUsed, *_parent.baseFee, _parent.blobGasUsed, parentExtra, parentIsHolocene,
            parentIsJovian, fallbackDenominator, _config.eip1559Elasticity);
        if (*_header.baseFee != expected)
        {
            return "baseFee does not match the OP EIP-1559 recomputation (have " +
                   _header.baseFee->str() + ", want " + expected.str() + ")";
        }
    }
    catch (std::exception const& e)
    {
        return std::string("OP base-fee recomputation failed: ") + e.what();
    }
    return std::nullopt;
}

/// extraData shape — op-geth eip1559.ValidateOptimismExtraData, keyed on the HEADER's own
/// timestamp: Jovian requires exactly 17 bytes (version 0x01), Holocene exactly 9 bytes
/// (version 0x00), both with a non-zero denominator/elasticity pair; before Holocene the
/// extraData must be EMPTY (the genesis block is exempt there, but the genesis header is
/// never validated by this function — it has no parent and is pinned by the
/// [eth_genesis_header] hash check instead). The generic ≤32-byte bound
/// (params.MaximumExtraDataSize) applies on every fork.
inline std::optional<std::string> validateOpExtraData(
    bcos::protocol::EthBlockHeaderData const& _header, OpChainConfig const& _config)
{
    if (_header.extraData.size() > kMaxExtraDataSize)
    {
        return "extraData exceeds the 32-byte bound";
    }
    OpFork const fork =
        bcos::ledger::resolveOpFork(_config.forkSchedule, static_cast<uint64_t>(_header.timestamp));
    if (fork >= OpFork::Jovian)
    {
        if (_header.extraData.size() != bcos::engine::c_jovianExtraDataBytes)
        {
            return "Jovian extraData should be 17 bytes, got " +
                   std::to_string(_header.extraData.size());
        }
        if (_header.extraData[0] != bcos::engine::c_jovianExtraDataVersion)
        {
            return "Jovian extraData version byte should be 1";
        }
        auto [denominator, elasticity] = bcos::engine::decodeEip1559Params(
            std::span<const bcos::byte>(_header.extraData.data(), _header.extraData.size())
                .subspan(1, bcos::engine::c_eip1559ParamsBytes));
        if (denominator == 0 || elasticity == 0)
        {
            return "Jovian extraData must encode a non-zero EIP-1559 denominator and elasticity";
        }
        return std::nullopt;
    }
    if (fork >= OpFork::Holocene)
    {
        if (_header.extraData.size() != bcos::engine::c_holoceneExtraDataBytes)
        {
            return "Holocene extraData should be 9 bytes, got " +
                   std::to_string(_header.extraData.size());
        }
        if (_header.extraData[0] != bcos::engine::c_holoceneExtraDataVersion)
        {
            return "Holocene extraData version byte should be 0";
        }
        auto [denominator, elasticity] = bcos::engine::decodeEip1559Params(
            std::span<const bcos::byte>(_header.extraData.data(), _header.extraData.size())
                .subspan(1, bcos::engine::c_eip1559ParamsBytes));
        if (denominator == 0 || elasticity == 0)
        {
            return "Holocene extraData must encode a non-zero EIP-1559 denominator and elasticity";
        }
        return std::nullopt;
    }
    if (!_header.extraData.empty())
    {
        return "extraData must be empty before Holocene";
    }
    return std::nullopt;
}

/// Fork-gated field presence, fail-closed in both directions — op-geth
/// beacon.verifyHeader (withdrawalsHash keyed on Shanghai == Canyon, Cancun fields keyed
/// on Cancun == Ecotone), op-geth beacon.FinalizeAndAssemble (pre-Isthmus withdrawalsHash
/// is the empty-list hash, Isthmus+ the L2ToL1MessagePasser storage root — unchecked
/// here), op-geth eip4844.VerifyEIP4844Header / CalcExcessBlobGas (OP short-circuits
/// excessBlobGas to 0; blobGasUsed is 0 until Jovian turns it into the DA footprint) and
/// the Isthmus requestsHash == sha256("") of the OP payload format.
inline std::optional<std::string> validateOpForkFields(
    bcos::protocol::EthBlockHeaderData const& _header, OpChainConfig const& _config)
{
    OpFork const fork =
        bcos::ledger::resolveOpFork(_config.forkSchedule, static_cast<uint64_t>(_header.timestamp));
    bool const canyon = fork >= OpFork::Canyon;
    bool const ecotone = fork >= OpFork::Ecotone;
    bool const isthmus = fork >= OpFork::Isthmus;
    bool const jovian = fork >= OpFork::Jovian;

    if (canyon && !_header.withdrawalsHash.has_value())
    {
        return "missing withdrawalsHash (Canyon active)";
    }
    if (!canyon && _header.withdrawalsHash.has_value())
    {
        return "withdrawalsHash present before Canyon";
    }
    if (canyon && !isthmus && *_header.withdrawalsHash != c_opEmptyWithdrawalsHash)
    {
        return "pre-Isthmus withdrawalsHash must be the empty withdrawals hash";
    }
    if (!ecotone)
    {
        if (_header.blobGasUsed.has_value() || _header.excessBlobGas.has_value() ||
            _header.parentBeaconRoot.has_value())
        {
            return "blobGasUsed/excessBlobGas/parentBeaconRoot present before Ecotone";
        }
    }
    else
    {
        if (!_header.blobGasUsed.has_value() || !_header.excessBlobGas.has_value() ||
            !_header.parentBeaconRoot.has_value())
        {
            return "missing blobGasUsed/excessBlobGas/parentBeaconRoot (Ecotone active)";
        }
        // op-geth CalcExcessBlobGas: OP-Stack chains don't support blobs and the excess
        // short-circuits to 0 on every fork (Jovian moves the DA footprint into
        // blobGasUsed, leaving excessBlobGas at 0).
        if (*_header.excessBlobGas != 0)
        {
            return "excessBlobGas must be 0 on OP chains";
        }
        if (!jovian && *_header.blobGasUsed != 0)
        {
            return "blobGasUsed must be 0 before Jovian";
        }
    }
    if (isthmus)
    {
        if (!_header.requestsHash.has_value())
        {
            return "missing requestsHash (Isthmus active)";
        }
        if (*_header.requestsHash != c_opEmptyRequestsHash)
        {
            return "requestsHash must be sha256(\"\") on OP chains (no execution requests)";
        }
    }
    else if (_header.requestsHash.has_value())
    {
        return "requestsHash present before Isthmus";
    }
    return std::nullopt;
}
}  // namespace detail

/// Validate `_header` against its parent per OP Stack consensus rules (post-Bedrock
/// chains are post-merge from genesis). Chain continuity (number/parentHash) is checked
/// by the caller; the genesis header never reaches this function — like on the L1 path it
/// is pinned by the [eth_genesis_header] hash check in Ledger instead.
///
/// Deliberate deviations from the L1 validator (validateHeaderPoS), all sourced from
/// op-geth:
///  - timestamp: op-geth beacon.verifyHeader only requires header.Time > parent.Time;
///    the fixed 2-second cadence is an op-node derivation rule, NOT an EL header rule.
///    A deviation from `parent + blockTimeSeconds` is therefore logged at WARNING level,
///    not failed.
///  - gasLimit: op-geth eip1559.VerifyEIP1559Header skips misc.VerifyGaslimit for
///    Optimism chains ("OP Stack gasLimit can adjust instantly" — the SystemConfig can
///    retarget it between adjacent blocks), so the 1/1024 adjacency bound does not apply;
///    the 5000 lower bound and the 2^63-1 upper bound (params.MaxGasLimit) are kept.
///  - coinbase: not part of op-geth header validation at all; a non-SequencerFeeVault
///    coinbase is logged at WARNING level only, once per process (per-header logging
///    would spam the whole sync on a chain that legitimately differs).
inline HeaderValidationResult validateOpHeader(bcos::protocol::EthBlockHeaderData const& _header,
    bcos::protocol::EthBlockHeaderData const& _parent, OpChainConfig const& _config)
{
    // PoS constants — OP chains are post-merge from genesis (Bedrock).
    if (_header.difficulty != 0)
    {
        return {false, "OP difficulty must be zero"};
    }
    if (_header.nonce != bcos::h64{})
    {
        return {false, "OP nonce must be zero"};
    }
    if (_header.uncleHash != bcos::protocol::c_emptyOmmersHash)
    {
        return {false, "OP blocks must have no ommers"};
    }
    // Number continuity and strictly-increasing timestamp (op-geth errInvalidTimestamp;
    // the 2s cadence is NOT enforced at EL level — see the file comment).
    if (_header.number != _parent.number + 1)
    {
        return {false, "header number must equal the parent number + 1"};
    }
    if (_header.timestamp <= _parent.timestamp)
    {
        return {false, "timestamp must be strictly greater than the parent"};
    }
    if (_config.blockTimeSeconds != 0 &&
        static_cast<uint64_t>(_header.timestamp - _parent.timestamp) != _config.blockTimeSeconds)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("OpHeaderValidator")
                          << LOG_DESC("non-standard OP block interval")
                          << LOG_KV("number", _header.number)
                          << LOG_KV("delta", _header.timestamp - _parent.timestamp)
                          << LOG_KV("expected", _config.blockTimeSeconds);
    }
    // Gas limits: the L1 1/1024 adjacency bound is intentionally absent (SystemConfig
    // retargeting — see the file comment).
    if (_header.gasLimit < kMinGasLimit)
    {
        return {false, "gasLimit below the 5000 minimum"};
    }
    if (_header.gasLimit > c_opMaxGasLimit)
    {
        return {false, "gasLimit above 2^63-1 (params.MaxGasLimit)"};
    }
    if (_header.gasUsed > _header.gasLimit)
    {
        return {false, "gasUsed exceeds gasLimit"};
    }
    if (_header.coinbase != c_opSequencerFeeVault)
    {
        // Once per process, not per header: this check runs on every downloaded
        // header, and a chain that legitimately carries a non-vault coinbase would
        // otherwise emit one WARNING per block for the whole sync. The function-
        // local static is deliberate — this validator is a header-only free
        // function (no instance state to hang a flag on) and is called from the
        // single-threaded sync loop today; the atomic keeps it sound if that ever
        // becomes multi-threaded.
        static std::atomic<bool> coinbaseWarned{false};
        if (!coinbaseWarned.exchange(true))
        {
            BCOS_LOG(WARNING) << LOG_BADGE("OpHeaderValidator")
                              << LOG_DESC("coinbase is not the SequencerFeeVault")
                              << LOG_KV("number", _header.number)
                              << LOG_KV("coinbase", _header.coinbase.hexPrefixed())
                              << LOG_KV("note", "further mismatches are not logged");
        }
    }

    // Fork-gated extraData shape (Holocene 9B / Jovian 17B / empty before Holocene).
    if (auto err = detail::validateOpExtraData(_header, _config))
    {
        return {false, *err};
    }
    // EIP-1559 base fee recomputation from the parent.
    if (auto err = detail::validateOpBaseFee(_header, _parent, _config))
    {
        return {false, *err};
    }
    // Canyon/Ecotone/Isthmus fork-gated field presence (fail-closed, both directions).
    if (auto err = detail::validateOpForkFields(_header, _config))
    {
        return {false, *err};
    }
    return {true, {}};
}

/// Convenience adaptor for HeaderChain::setHeaderValidator — binds the config into the
/// validator closure.
inline std::function<HeaderValidationResult(
    bcos::protocol::EthBlockHeaderData const&, bcos::protocol::EthBlockHeaderData const&)>
makeOpHeaderValidator(OpChainConfig _config)
{
    return [_config = std::move(_config)](bcos::protocol::EthBlockHeaderData const& _header,
               bcos::protocol::EthBlockHeaderData const& _parent) -> HeaderValidationResult {
        return validateOpHeader(_header, _parent, _config);
    };
}
}  // namespace bcos::devp2p::sync
