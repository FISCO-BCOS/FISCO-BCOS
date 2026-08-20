/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief
 * @file LedgerConfig.h
 * @author: yujiechen
 * @date 2021-05-06
 */
#pragma once
#include "../consensus/ConsensusNode.h"
#include "../protocol/ProtocolTypeDef.h"
#include "Features.h"
#include "SystemConfigs.h"
#include <evmc/evmc.hpp>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace bcos::ledger
{

/// Thrown when a persisted evmc_revision SYS_CONFIG value cannot be parsed. The EVM
/// revision is a consensus parameter on a v2 chain, so a corrupt value must halt loudly
/// (explicit startup failure) rather than fall back to a compile-time default that could
/// differ between binaries (a silent state-root fork).
DERIVE_BCOS_EXCEPTION(InvalidEVMCRevisionConfig);

constexpr static uint64_t DEFAULT_GAS_LIMIT = 3000000000;
constexpr static std::uint64_t DEFAULT_EPOCH_SEALER_NUM = 4;
constexpr static std::uint64_t DEFAULT_EPOCH_BLOCK_NUM = 1000;
constexpr static std::uint64_t DEFAULT_INTERNAL_NOTIFY_FLAG = 0;

class LedgerConfig
{
public:
    using Ptr = std::shared_ptr<LedgerConfig>;
    LedgerConfig() = default;
    LedgerConfig(const LedgerConfig&) = default;
    LedgerConfig(LedgerConfig&&) = default;
    LedgerConfig& operator=(const LedgerConfig&) = default;
    LedgerConfig& operator=(LedgerConfig&&) = default;
    virtual ~LedgerConfig() = default;

    virtual void setConsensusNodeList(bcos::consensus::ConsensusNodeList _consensusNodeList)
    {
        m_consensusNodeList = std::move(_consensusNodeList);
    }
    virtual void setObserverNodeList(bcos::consensus::ConsensusNodeList _observerNodeList)
    {
        m_observerNodeList = std::move(_observerNodeList);
    }
    virtual void setCandidateSealerNodeList(
        bcos::consensus::ConsensusNodeList candidateSealerNodeList)
    {
        m_candidateSealerNodeList = std::move(candidateSealerNodeList);
    }
    virtual void setHash(bcos::crypto::HashType const& _hash) { m_hash = _hash; }
    virtual void setTimestamp(int64_t _timestamp) { m_timestamp = _timestamp; }
    virtual void setBlockNumber(bcos::protocol::BlockNumber _blockNumber)
    {
        m_blockNumber = _blockNumber;
    }
    virtual void setBlockTxCountLimit(uint64_t _blockTxCountLimit)
    {
        m_blockTxCountLimit = _blockTxCountLimit;
    }

    virtual bcos::consensus::ConsensusNodeList const& consensusNodeList() const
    {
        return m_consensusNodeList;
    }

    virtual bcos::consensus::ConsensusNodeList& mutableConsensusNodeList()
    {
        return m_consensusNodeList;
    }

    virtual bcos::consensus::ConsensusNodeList const& observerNodeList() const
    {
        return m_observerNodeList;
    }
    virtual bcos::consensus::ConsensusNodeList const& candidateSealerNodeList() const
    {
        return m_candidateSealerNodeList;
    }
    bcos::crypto::HashType const& hash() const { return m_hash; }
    bcos::protocol::BlockNumber blockNumber() const { return m_blockNumber; }

    int64_t timestamp() const { return m_timestamp; }

    void setConsensusType(const std::string& _consensusType) { m_consensusType = _consensusType; }
    std::string consensusType() const { return m_consensusType; }

    uint64_t blockTxCountLimit() const { return m_blockTxCountLimit; }

    bcos::consensus::ConsensusNodeList& mutableConsensusList() { return m_consensusNodeList; }
    bcos::consensus::ConsensusNodeList& mutableObserverList() { return m_observerNodeList; }
    bcos::consensus::ConsensusNodeList& mutableCandidateSealerNodeList()
    {
        return m_candidateSealerNodeList;
    }

    uint64_t leaderSwitchPeriod() const { return m_leaderSwitchPeriod; }
    void setLeaderSwitchPeriod(uint64_t _leaderSwitchPeriod)
    {
        m_leaderSwitchPeriod = _leaderSwitchPeriod;
    }

    std::tuple<uint64_t, protocol::BlockNumber> const& gasLimit() const { return m_gasLimit; }
    void setGasLimit(std::tuple<uint64_t, protocol::BlockNumber> mGasLimit)
    {
        m_gasLimit = std::move(mGasLimit);
    }

    std::tuple<std::string, protocol::BlockNumber> const& gasPrice() const { return m_gasPrice; }
    void setGasPrice(std::tuple<std::string, protocol::BlockNumber> mGasPrice)
    {
        m_gasPrice = std::move(mGasPrice);
    }

    int64_t difficulty() const { return m_difficulty; }
    void setDifficulty(int64_t d) { m_difficulty = d; }

    // EIP-4399 prev_randao (block mixHash). Used by the PREVRANDAO/DIFFICULTY
    // opcode for Paris+ revisions. Kept in the ledger config because the BCOS
    // block header has no dedicated mixHash/random field.
    evmc::bytes32 prevRandao() const { return m_prevRandao; }
    void setPrevRandao(evmc::bytes32 v) { m_prevRandao = v; }

    // EIP-4844 blob gas parameters (Cancun+). The BCOS block header has no
    // dedicated fields, so EEST runner stores them here.
    std::optional<uint64_t> excessBlobGas() const { return m_excessBlobGas; }
    void setExcessBlobGas(std::optional<uint64_t> v) { m_excessBlobGas = v; }
    std::optional<uint64_t> blobGasUsed() const { return m_blobGasUsed; }
    void setBlobGasUsed(std::optional<uint64_t> v) { m_blobGasUsed = v; }

    // Not enforce to set this field, in memory data
    void setSealerId(int64_t _sealerId) { m_sealerId = _sealerId; }
    int64_t sealerId() const { return m_sealerId; }

    // Not enforce to set this field, in memory data
    void setTxsSize(int64_t _txsSize) { m_txsSize = _txsSize; }
    int64_t txsSize() const { return m_txsSize; }

    void setCompatibilityVersion(uint32_t _version) { m_compatibilityVersion = _version; }
    uint32_t compatibilityVersion() const { return m_compatibilityVersion; }

    void setAuthCheckStatus(uint32_t _authStatus) { m_authCheckStatus = _authStatus; }
    uint32_t authCheckStatus() const { return m_authCheckStatus; }

    void setEpochSealerNum(std::tuple<uint64_t, protocol::BlockNumber> _epochSealerNum)
    {
        m_epochSealerNum = _epochSealerNum;
    }
    std::tuple<uint64_t, protocol::BlockNumber> const& epochSealerNum() const
    {
        return m_epochSealerNum;
    }

    void setEpochBlockNum(std::tuple<uint64_t, protocol::BlockNumber> _epochBlockNum)
    {
        m_epochBlockNum = _epochBlockNum;
    }
    std::tuple<uint64_t, protocol::BlockNumber> const& epochBlockNum() const
    {
        return m_epochBlockNum;
    }

    void setNotifyRotateFlagInfo(const uint64_t _notifyRotateFlagInfo)
    {
        m_notifyRotateFlagInfo = _notifyRotateFlagInfo;
    }
    uint64_t notifyRotateFlagInfo() const { return m_notifyRotateFlagInfo; }

    Features const& features() const { return m_features; }
    void setFeatures(Features features) { m_features = features; }

    std::optional<evmc_uint256be> const& chainId() const { return m_chainId; }
    void setChainId(evmc_uint256be _chainId) { m_chainId = _chainId; }

    bool balanceTransfer() const { return m_balanceTransfer; }
    void setBalanceTransfer(bool _balanceTransfer) { m_balanceTransfer = _balanceTransfer; }
    int executorVersion() const { return m_executorVersion; }
    void setExecutorVersion(int _executorVersion) { m_executorVersion = _executorVersion; }

    /// Ethereum mode: explicit EVMC revision override (bypasses toRevision feature mapping).
    /// When set, HostContext and TransactionExecutor use this revision directly.
    std::optional<evmc_revision> evmcRevision() const { return m_explicitRevision; }
    void setEVMCRevision(evmc_revision rev) { m_explicitRevision = rev; }

    /// Fork transition map: block number → EVMC revision.
    /// At or after the given block number, the corresponding revision is used.
    /// Example: {0: EVMC_CANCUN, 3: EVMC_PRAGUE} means blocks 0-2 use Cancun,
    /// blocks 3+ use Prague.
    void addForkTransition(bcos::protocol::BlockNumber blockNum, evmc_revision rev)
    {
        m_forkTransitions[blockNum] = rev;
    }
    void clearForkTransitions() { m_forkTransitions.clear(); }
    std::optional<evmc_revision> evmcRevisionForBlock(bcos::protocol::BlockNumber blockNum) const
    {
        if (m_forkTransitions.empty())
            return m_explicitRevision;
        // Find the latest transition point ≤ blockNum
        auto it = m_forkTransitions.upper_bound(blockNum);
        if (it == m_forkTransitions.begin())
            return m_explicitRevision;  // No transition before this block
        --it;
        return it->second;
    }

private:
    bcos::consensus::ConsensusNodeList m_consensusNodeList;
    bcos::consensus::ConsensusNodeList m_observerNodeList;
    bcos::consensus::ConsensusNodeList m_candidateSealerNodeList;
    bcos::crypto::HashType m_hash;
    bcos::protocol::BlockNumber m_blockNumber = 0;
    int64_t m_timestamp = 0;
    std::string m_consensusType;
    uint64_t m_blockTxCountLimit = 0;
    uint64_t m_leaderSwitchPeriod = 1;
    std::tuple<uint64_t, protocol::BlockNumber> m_gasLimit = {DEFAULT_GAS_LIMIT, 0};
    std::tuple<std::string, protocol::BlockNumber> m_gasPrice = {"0x0", 0};
    int64_t m_difficulty = 0;
    evmc::bytes32 m_prevRandao{};
    std::optional<uint64_t> m_excessBlobGas;
    std::optional<uint64_t> m_blobGasUsed;
    std::tuple<uint64_t, protocol::BlockNumber> m_epochSealerNum = {DEFAULT_EPOCH_SEALER_NUM, 0};
    std::tuple<uint64_t, protocol::BlockNumber> m_epochBlockNum = {DEFAULT_EPOCH_BLOCK_NUM, 0};
    uint64_t m_notifyRotateFlagInfo{0};
    // the compatibilityVersion
    // the system version, can only be upgraded manually
    uint32_t m_compatibilityVersion = 0;
    // no need to store, in memory data
    int64_t m_sealerId = -1;
    int64_t m_txsSize = -1;
    uint32_t m_authCheckStatus = 0;
    Features m_features;
    std::optional<evmc_uint256be> m_chainId;
    bool m_balanceTransfer = false;
    int m_executorVersion = 0;
    std::optional<evmc_revision> m_explicitRevision;
    std::map<bcos::protocol::BlockNumber, evmc_revision> m_forkTransitions;
};

/// The latest EVM revision supported by the ethereum-executor. This is a fallback only:
/// NodeConfig::loadExecutorConfig requires an explicit evm_revision for executor_version>=2,
/// so a fresh v2 chain always persists its revision on-chain, and the startup path refuses
/// to boot a v2 chain whose row is absent (no binary-side default). The constant applies
/// solely on the (defensive) v2-missing-config path in getLedgerConfig, plus test /
/// EEST-runner convenience.
inline constexpr evmc_revision EVMC_REVISION_DEFAULT = EVMC_OSAKA;

/// Executor version selecting the pure-Ethereum EthereumExecutor (ethereum-executor).
/// Canonical value kept here so lower layers (bcos-ledger, bcos-tool) can gate on it
/// without depending on libinitializer; libinitializer/MultiVersionScheduler.h keeps a
/// scheduler_v1-scoped alias for the same value. Versions >= this all select the v2
/// executor (setVersion saturates), leaving room above 2 for a future executor.
inline constexpr int ETHEREUM_EXECUTOR_VERSION = 2;

/// The executor version that selects the OP-Stack OpSchedulerSeam (op composition root).
/// executor_version >= this enters OP mode (spec 2026-08-07-op-composition-root-design.md D1).
inline constexpr int OPSTACK_EXECUTOR_VERSION = 3;

/// Convert a canonical EVM fork name (case-insensitive, e.g. "cancun"/"osaka") to an
/// EVMC revision. Returns nullopt for unknown names so callers can fall back to a default.
inline std::optional<evmc_revision> evmcRevisionFromName(std::string_view name)
{
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (n == "frontier")
        return EVMC_FRONTIER;
    if (n == "homestead")
        return EVMC_HOMESTEAD;
    if (n == "tangerinewhistle" || n == "tangerine whistle" || n == "eip150")
        return EVMC_TANGERINE_WHISTLE;
    if (n == "spuriousdragon" || n == "spurious dragon" || n == "eip158")
        return EVMC_SPURIOUS_DRAGON;
    if (n == "byzantium")
        return EVMC_BYZANTIUM;
    if (n == "constantinople")
        return EVMC_CONSTANTINOPLE;
    if (n == "petersburg" || n == "constantinoplefix")
        return EVMC_PETERSBURG;
    if (n == "istanbul" || n == "muirglacier" || n == "muir glacier")
        return EVMC_ISTANBUL;
    if (n == "berlin")
        return EVMC_BERLIN;
    if (n == "london")
        return EVMC_LONDON;
    if (n == "paris" || n == "merge")
        return EVMC_PARIS;
    if (n == "shanghai")
        return EVMC_SHANGHAI;
    if (n == "cancun")
        return EVMC_CANCUN;
    if (n == "prague")
        return EVMC_PRAGUE;
    if (n == "osaka")
        return EVMC_OSAKA;
    if (n == "experimental")
        return EVMC_EXPERIMENTAL;
    return std::nullopt;
}

/// Canonical (space-free) fork name for an EVMC revision, used when serializing the
/// evmc_revision system config value.
///
/// The switch is deliberately total (no `default:`): every evmc_revision enumerator is
/// listed, so a future evmc bump that adds a revision becomes a compile error pointing
/// right here instead of silently mislabelling it (e.g. as "osaka") on-chain.
inline std::string_view evmcRevisionName(evmc_revision rev)
{
    switch (rev)
    {
    case EVMC_FRONTIER:
        return "frontier";
    case EVMC_HOMESTEAD:
        return "homestead";
    case EVMC_TANGERINE_WHISTLE:
        return "tangerinewhistle";
    case EVMC_SPURIOUS_DRAGON:
        return "spuriousdragon";
    case EVMC_BYZANTIUM:
        return "byzantium";
    case EVMC_CONSTANTINOPLE:
        return "constantinople";
    case EVMC_PETERSBURG:
        return "petersburg";
    case EVMC_ISTANBUL:
        return "istanbul";
    case EVMC_BERLIN:
        return "berlin";
    case EVMC_LONDON:
        return "london";
    case EVMC_PARIS:
        return "paris";
    case EVMC_SHANGHAI:
        return "shanghai";
    case EVMC_CANCUN:
        return "cancun";
    case EVMC_PRAGUE:
        return "prague";
    case EVMC_OSAKA:
        return "osaka";
    case EVMC_EXPERIMENTAL:
        return "experimental";
    }
    // Unreachable: the switch above is exhaustive over evmc_revision.
    return {};
}

/// Serialize an EVMC revision config (explicit revision + fork transitions) to the
/// SYS_CONFIG value string. Format: a comma-separated list of "block:forkName" entries.
/// The first entry is always the block-0 base revision, so decoding always yields a
/// complete fork schedule.
inline std::string encodeEVMCRevisionConfig(
    std::optional<evmc_revision> explicitRev,
    std::map<bcos::protocol::BlockNumber, evmc_revision> const& forks)
{
    evmc_revision base = EVMC_REVISION_DEFAULT;
    if (auto it = forks.find(0); it != forks.end())
    {
        base = it->second;
    }
    else if (explicitRev)
    {
        base = *explicitRev;
    }
    else if (!forks.empty())
    {
        base = forks.begin()->second;
    }

    std::ostringstream oss;
    oss << "0:" << evmcRevisionName(base);
    for (auto const& [block, rev] : forks)
    {
        if (block <= 0)
        {
            continue;  // block-0 entry is already emitted as the base
        }
        oss << "," << block << ":" << evmcRevisionName(rev);
    }
    return oss.str();
}

/// Parse a SYS_CONFIG value string produced by encodeEVMCRevisionConfig and populate
/// @p ledgerConfig's EVMC revision settings (explicit revision + fork transitions).
inline void applyEVMCRevisionConfig(LedgerConfig& ledgerConfig, std::string_view value)
{
    ledgerConfig.clearForkTransitions();
    std::optional<evmc_revision> base;
    std::optional<evmc_revision> single;

    std::string_view::size_type pos = 0;
    while (pos < value.size())
    {
        auto comma = value.find(',', pos);
        auto entry = value.substr(pos,
            comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        pos = comma == std::string_view::npos ? value.size() : comma + 1;

        auto colon = entry.find(':');
        if (colon == std::string_view::npos)
        {
            // Bare fork name -> explicit single revision for all blocks.
            if (auto rev = evmcRevisionFromName(entry); rev)
            {
                single = *rev;
            }
            continue;
        }

        auto blockStr = entry.substr(0, colon);
        auto name = entry.substr(colon + 1);
        bcos::protocol::BlockNumber block = 0;
        auto [ptr, ec] =
            std::from_chars(blockStr.data(), blockStr.data() + blockStr.size(), block);
        if (ec != std::errc())
        {
            continue;
        }
        if (auto rev = evmcRevisionFromName(name); rev)
        {
            ledgerConfig.addForkTransition(block, *rev);
            if (!base || block == 0)
            {
                base = *rev;
            }
        }
    }

    if (single)
    {
        // A single explicit revision covers every block.
        ledgerConfig.clearForkTransitions();
        ledgerConfig.setEVMCRevision(*single);
    }
    else if (base)
    {
        // Expose the block-0 base (or the earliest fork) through the explicit slot so
        // evmcRevisionForBlock has a fallback for blocks before the first fork.
        ledgerConfig.setEVMCRevision(*base);
    }
    else
    {
        // Nothing parsed (empty value, or every entry failed from_chars /
        // evmcRevisionFromName). The EVM revision is a consensus parameter on a v2
        // chain, so a corrupt persisted value must halt loudly, not silently fall back
        // to a compile-time default: two nodes on different binaries would otherwise
        // execute the same blocks under different revisions (a silent state-root fork).
        BOOST_THROW_EXCEPTION(InvalidEVMCRevisionConfig() << errinfo_comment(
            "cannot parse evmc_revision config value: " + std::string(value)));
    }
}
}  // namespace bcos::ledger
