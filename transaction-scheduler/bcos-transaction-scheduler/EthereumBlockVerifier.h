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
 * @file EthereumBlockVerifier.h
 * @brief External Ethereum block verification core: executes an incoming block
 *        (from devp2p sync or the Engine API) against the local state — including
 *        the Cancun/Prague block-level system calls (EIP-4788/2935 at block start,
 *        EIP-7002/7251 at block end, see EthereumSystemCalls.h) — checks
 *        the deterministic roots (txsRoot/receiptsRoot/gasUsed/logsBloom), the
 *        uncle (ommers) hash, the withdrawals root and the state root, then
 *        commits the block + ledger rows atomically (FIB-104 prewriteBlockToBuffer
 *        pattern).
 * @date 2026/8/18
 */
#pragma once

#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/EthTrieRoots.h"
#include "bcos-ledger/mpt/EthereumBlockRoots.h"
#include "bcos-ledger/mpt/StateRoots.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthWithdrawal.h"
#include "bcos-task/Task.h"
#include "bcos-transaction-scheduler/EthereumSystemCalls.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <evmc/evmc.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::scheduler_v1
{

// Timestamp-based Ethereum fork schedule (Sepolia / Holesky / Mainnet PoS chains).
// A zero timestamp means the fork is active from genesis; the DEFAULT is
// UINT64_MAX = "never active", so a default-constructed (or partially filled) schedule
// can never accidentally activate every fork — callers that mean "active from genesis"
// must set the field to 0 explicitly. (The production path goes through NodeConfig,
// which maps missing keys to UINT64_MAX, matching this default.)
struct EvmcForkTimestamps
{
    static constexpr uint64_t kForkDisabled = std::numeric_limits<uint64_t>::max();
    uint64_t londonTime{kForkDisabled};    // EIP-1559
    uint64_t parisTime{kForkDisabled};     // The Merge (PoS)
    uint64_t shanghaiTime{kForkDisabled};  // EIP-4895 withdrawals
    uint64_t cancunTime{kForkDisabled};    // EIP-4844 blobs
    uint64_t pragueTime{kForkDisabled};    // EIP-7702 etc.
    uint64_t osakaTime{kForkDisabled};
    // EIP-7892 blob-parameter-only forks: no EVM revision change (execution
    // stays Osaka), but they bump the EIP-7840 blob schedule — the executor
    // picks it up through ledgerConfig.blobSchedule, stamped per block by
    // fillExecutionLedgerConfig below.
    uint64_t bpo1Time{kForkDisabled};
    uint64_t bpo2Time{kForkDisabled};
};

inline constexpr uint64_t kSecondsToMilliseconds = 1000;
inline constexpr size_t kHashBytes = 32;
inline constexpr size_t kChainIdBytes = 8;

/// The EVMC revision active for a block with the given timestamp. A zero fork
/// timestamp means "active from genesis" (consistent with HeaderValidator's
/// isForkActive semantics); an unset field (the UINT64_MAX default) means the fork
/// never activates.
///
/// Paris (the Merge) is special: geth activates it at paris_time OR as soon as
/// the chain's Terminal Total Difficulty has been reached — a PoW-configured
/// chain then mints blocks whose difficulty is 0. From that block onwards the
/// 0x44 opcode is PREVRANDAO (returns the mixHash) instead of DIFFICULTY
/// (EIP-4399). On Sepolia the TTD is reached at block 1450409, months before
/// the configured paris_time (block 1735371): those intermediate blocks carry
/// timestamp < paris_time but must execute with EVMC_PARIS. A block with
/// difficulty 0 is the per-block signal that the TTD has been passed. (A pure
/// PoS chain, parisTime == 0, is already covered by the forkTime == 0 rule;
/// its blocks are all difficulty 0 too, so the check is harmless there.)
inline evmc_revision evmcRevisionForTimestamp(
    EvmcForkTimestamps const& schedule, int64_t timestamp, u256 const& difficulty)
{
    const uint64_t timestampValue = static_cast<uint64_t>(timestamp);
    auto active = [timestampValue](uint64_t forkTime) {
        return forkTime == 0 || timestampValue >= forkTime;
    };
    if (active(schedule.osakaTime))
    {
        return EVMC_OSAKA;
    }
    if (active(schedule.pragueTime))
    {
        return EVMC_PRAGUE;
    }
    if (active(schedule.cancunTime))
    {
        return EVMC_CANCUN;
    }
    if (active(schedule.shanghaiTime))
    {
        return EVMC_SHANGHAI;
    }
    // Paris: paris_time reached, or TTD passed (difficulty == 0 on a PoW chain).
    if (active(schedule.parisTime) || difficulty == 0)
    {
        return EVMC_PARIS;
    }
    // Pre-Paris (PoW phase, e.g. Sepolia before 2022-08-22): London is the
    // latest pre-merge revision on these chains (london_time is 0 = genesis).
    return EVMC_LONDON;  // pre-Paris fallback
}

/// u256 -> "0x"-prefixed lowercase hex string (the format buildBlockInfo parses).
inline std::string u256ToHexString(u256 const& value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << value;
    return oss.str();
}

/// Build the execution BlockHeader from an external Ethereum header. The timestamp is
/// converted seconds -> milliseconds (buildBlockInfo divides by 1000). The roots/gasUsed
/// are what we are about to verify, so they are left at their defaults here.
inline protocol::BlockHeader::Ptr makeExecutionBlockHeader(
    protocol::EthBlockHeaderData const& ethHeader, protocol::BlockFactory& blockFactory,
    uint32_t blockVersion)
{
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(ethHeader.number);
    header->setTimestamp(static_cast<int64_t>(ethHeader.timestamp) *
                         static_cast<int64_t>(kSecondsToMilliseconds));
    header->setVersion(blockVersion);
    protocol::ParentInfo parentInfo{
        .blockNumber = ethHeader.number - 1, .blockHash = ethHeader.parentInfo.blockHash};
    header->setParentInfo(parentInfo);
    header->setCoinbase(ethHeader.coinbase);
    header->setPrevRandao(ethHeader.prevRandao);
    header->setGasLimit(ethHeader.gasLimit);
    // Carry the FULL Ethereum header in the Tars header so the committed ledger
    // block round-trips back to the exact same RLP on resume (the resume anchor
    // re-encodes the stored header and must reproduce the chain's header hash).
    header->setStateRoot(ethHeader.stateRoot);
    header->setTxsRoot(ethHeader.txsRoot);
    header->setReceiptsRoot(ethHeader.receiptsRoot);
    header->setLogsBloom(bcos::bytesConstRef(ethHeader.logsBloom.data(), ethHeader.logsBloom.size()));
    header->setDifficulty(ethHeader.difficulty);
    header->setGasUsed(ethHeader.gasUsed);
    header->setNonce(ethHeader.nonce);
    header->setExtraData(ethHeader.extraData);
    header->setUncleHash(ethHeader.uncleHash);
    if (ethHeader.baseFee)
    {
        header->setBaseFee(*ethHeader.baseFee);
    }
    if (ethHeader.withdrawalsHash)
    {
        header->setWithdrawalsRoot(*ethHeader.withdrawalsHash);
    }
    if (ethHeader.blobGasUsed)
    {
        header->setBlobGasUsed(*ethHeader.blobGasUsed);
    }
    if (ethHeader.excessBlobGas)
    {
        header->setExcessBlobGas(*ethHeader.excessBlobGas);
    }
    if (ethHeader.parentBeaconRoot)
    {
        header->setParentBeaconBlockRoot(*ethHeader.parentBeaconRoot);
    }
    if (ethHeader.requestsHash)
    {
        header->setRequestsHash(*ethHeader.requestsHash);
    }
    return header;
}

/// Overlay the per-block Ethereum EVM context (base fee, blob gas, prevRandao,
/// difficulty, chain id, revision) onto a LedgerConfig fetched from storage via
/// ledger::getLedgerConfig. The overlay wins over the system-config values.
inline void fillExecutionLedgerConfig(protocol::EthBlockHeaderData const& ethHeader,
    ledger::LedgerConfig& config, EvmcForkTimestamps const& schedule, uint64_t chainId)
{
    config.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
    config.setEVMCRevision(
        evmcRevisionForTimestamp(schedule, ethHeader.timestamp, ethHeader.difficulty));
    // Block gas limit: the EVM's GASLIMIT opcode (0x45) must return the BLOCK's
    // gas limit (header.gasLimit), NOT the chain's tx_gas_limit system config.
    // geth's BlockContext.GasLimit is the header field; on Sepolia it is 30,000,000
    // while the FISCO system-config default is 3,000,000,000 — using the latter
    // makes contracts that store block.gasLimit fork (observed at block 2064597:
    // contract wrote 2998914560 instead of 30000000).
    config.setGasLimit({static_cast<uint64_t>(ethHeader.gasLimit), ethHeader.number});
    // Difficulty: real value for PoW (pre-merge) blocks — the EVM's DIFFICULTY
    // opcode (0x44) must return it, and the pre-Paris host maps it into
    // prev_randao (buildBlockInfo). PoS (merge+) blocks have difficulty 0.
    // Sepolia's merge block is 1735371; blocks below it are PoW.
    config.setDifficulty(ethHeader.difficulty > std::numeric_limits<int64_t>::max() ?
                              std::numeric_limits<int64_t>::max() :
                              static_cast<int64_t>(ethHeader.difficulty));

    evmc::bytes32 randao{};
    std::memcpy(randao.bytes, ethHeader.prevRandao.data(), kHashBytes);
    config.setPrevRandao(randao);

    // EIP-1559 base fee; "0x0" when absent (pre-London, though PoS chains are London+).
    config.setGasPrice(
        {ethHeader.baseFee ? u256ToHexString(*ethHeader.baseFee) : "0x0", ethHeader.number});

    // EIP-4844 blob gas (Cancun+).
    if (ethHeader.excessBlobGas)
    {
        config.setExcessBlobGas(static_cast<uint64_t>(*ethHeader.excessBlobGas));
    }
    if (ethHeader.blobGasUsed)
    {
        config.setBlobGasUsed(static_cast<uint64_t>(*ethHeader.blobGasUsed));
    }

    // EIP-7840 blob schedule for THIS block, resolved from the fork-timestamp
    // schedule: the executor's blob base fee and per-block blob gas limit read
    // it (an evmc revision tops out at Osaka and cannot express the BPO1/BPO2
    // schedule bumps). Stamped unconditionally — the executor only consults it
    // from Cancun on, and the zero entry pre-Cancun falls through to the
    // revision-keyed default.
    config.setBlobSchedule(protocol::blobScheduleForTimestamp(
        protocol::BlobForkTimes{.cancunTime = schedule.cancunTime,
            .pragueTime = schedule.pragueTime,
            .bpo1Time = schedule.bpo1Time,
            .bpo2Time = schedule.bpo2Time},
        static_cast<uint64_t>(ethHeader.timestamp)));

    // chain id -> big-endian evmc_uint256be.
    evmc_uint256be cid{};
    bcos::bytes chainIdBytes(kChainIdBytes, 0);
    bcos::toBigEndian(chainId, chainIdBytes);
    std::memcpy(cid.bytes + (kHashBytes - kChainIdBytes), chainIdBytes.data(), kChainIdBytes);
    config.setChainId(cid);
}

/// The deterministic post-execution block values that must match the header.
/// The definition lives in bcos-ledger/mpt/EthereumBlockRoots.h, shared with the Engine
/// API block builder (EngineServiceImpl::buildPayload).
using EthereumBlockComputation = ledger::mpt::EthereumBlockComputation;

// PoW (pre-merge) block reward: 2 ETH per block (Constantinople+), plus 1/32 ETH
// per included uncle for the coinbase, and (uncle.number + 8 - block.number) * 2
// ETH / 8 for each uncle's miner (geth's accumulateRewards).
inline constexpr u256 kPoWBlockReward{2000000000000000000ull};  // 2 * 10^18 wei

// EIP-4844: blob gas per blob (2**17). Mirrors GAS_PER_BLOB in
// ethereum-executor/EVMSupport.h and kGasPerBlob in devp2p's HeaderValidator.h.
inline constexpr u256 kBlobGasPerBlob{131072};

/// Accumulate PoW block rewards (coinbase + uncles) into `view`, exactly like
/// geth's `accumulateRewards`. Only meaningful for pre-merge (PoW) blocks; PoS
/// blocks pay no rewards (the CL handles them via withdrawals).
template <class ViewType>
task::Task<void> accumulatePoWBlockRewards(ViewType& view,
    protocol::EthBlockHeaderData const& ethHeader,
    std::vector<bcos::bytes> const& rawUncles, ledger::LedgerConfig const& ledgerConfig)
{
    using namespace bcos::ledger::account;
    const bool binaryAddress =
        ledgerConfig.features().get(ledger::Features::Flag::feature_raw_address);

    auto addBalance = [&](bcos::Address const& address, u256 const& amount) -> task::Task<void> {
        // EL mode: a FISCO system-address (e.g. 0x...1000) is an ordinary Ethereum account and
        // must live under /apps/ so the MPT builder sees it. Pass treatSystemAsUser=true.
        EVMAccount<ViewType> account(view, address, binaryAddress, /*treatSystemAsUser=*/true);
        // Register the account table (SYS_TABLES) so the executor's
        // readAccountImpl can see it: it decides existence via the flat fields,
        // but the write-back path (applyToStorage) still needs a registered
        // table for the account to persist. A coinbase/uncle address that only
        // ever received block rewards would otherwise stay unregistered.
        if (!co_await account.exists())
        {
            co_await account.create();
        }
        u256 balance = co_await account.balance();
        co_await account.setBalance(balance + amount);
    };

    // Uncle inclusion rewards: the miner of each uncle gets
    // (uncle.number + 8 - block.number) * blockReward / 8; the coinbase gets
    // blockReward / 32 per included uncle.
    u256 coinbaseReward = kPoWBlockReward;
    for (auto const& uncleRlp : rawUncles)
    {
        protocol::EthBlockHeader uncle;
        if (auto err = uncle.rlpDecode(bcos::bytesConstRef(uncleRlp.data(), uncleRlp.size())))
        {
            // A malformed uncle must not be silently skipped: its inclusion reward is
            // part of the world state, so skipping it would compute a wrong state root
            // silently. Throwing here makes the block invalid (the verifyAndCommit
            // caller turns the exception into an invalid-block result).
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"EthereumBlockVerifier: malformed uncle header RLP: " +
                                   err->errorMessage()});
        }
        auto const& uncleHeader = uncle.data();
        // Depth bounds before any reward arithmetic (geth's accumulateRewards relies
        // on verifyUncles having enforced them): the uncle must be strictly older than
        // the block and within 8 blocks of depth. Compute in SIGNED arithmetic — the
        // reward formula below runs on u256 (unsigned), so an out-of-range uncle would
        // otherwise underflow into an astronomical reward.
        auto const depth = ethHeader.number - uncleHeader.number;
        if (uncleHeader.number >= ethHeader.number || depth > 8)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{
                "EthereumBlockVerifier: uncle number out of the valid depth range (uncle " +
                std::to_string(uncleHeader.number) + ", block " +
                std::to_string(ethHeader.number) + ")"});
        }
        u256 uncleReward =
            u256(static_cast<uint64_t>(8 - depth)) * kPoWBlockReward / 8;
        co_await addBalance(uncleHeader.coinbase, uncleReward);
        coinbaseReward += kPoWBlockReward / 32;
    }
    co_await addBalance(ethHeader.coinbase, coinbaseReward);
}

/// keccak256(rlp(uncles)) — the header's uncleHash (ommers) commitment. Each rawUncles
/// element is a COMPLETE RLP item (the uncle header's own RLP), so the list encoding is
/// their plain concatenation under a single list header. The empty list encodes to 0xc0,
/// whose keccak256 is the canonical empty-ommers hash (bcos::protocol::c_emptyOmmersHash,
/// single-sourced in bcos-rlp-protocol/EthBlockHeader.h) — the value every PoS header
/// carries, so comparing this against the header's uncleHash also enforces "PoS blocks
/// must have no uncles" with no extra rule.
inline crypto::HashType calculateUnclesHash(std::vector<bcos::bytes> const& rawUncles)
{
    if (rawUncles.empty())
    {
        return protocol::c_emptyOmmersHash;
    }
    bcos::bytes payload;
    for (auto const& uncle : rawUncles)
    {
        payload.insert(payload.end(), uncle.begin(), uncle.end());
    }
    bcos::bytes encoded;
    encoded.reserve(payload.size() + 8);
    bcos::codec::rlp::encodeHeader(
        encoded, bcos::codec::rlp::Header{.isList = true, .payloadLength = payload.size()});
    encoded.insert(encoded.end(), payload.begin(), payload.end());
    return bcos::crypto::keccak256Hash(bcos::bytesConstRef(encoded.data(), encoded.size()));
}

struct EthereumBlockVerificationResult
{
    bool valid{false};
    std::string error;
    protocol::BlockHeader::Ptr header;                       ///< the FISCO execution header
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    std::vector<protocol::Transaction::Ptr> transactions;
    EthereumBlockComputation computation;
    crypto::HashType stateRoot;
};

/// Verifies and commits one external Ethereum block. Shared by the devp2p sync path
/// and the Engine API external-payload path.
///
/// The state root is computed over the Ethereum world state (accounts + storage) via the
/// ledger MPT builder when the executor version is >= ETHEREUM_EXECUTOR_VERSION (v2); the
/// injected calculator is only a fallback for legacy (v1) executions.
///
/// NOTE: the MPT build is INCREMENTAL — it needs the parent block's trie nodes resolvable
/// through the executed view (persisted by the previous block's commit), so blocks must be
/// verified strictly in order from a known state root.
template <class Scheduler, class Executor>
class EthereumBlockVerifier
{
public:
    using TransactionDecoder = std::function<protocol::Transaction::Ptr(bcos::bytes const&)>;
    template <class Storage>
    using StateRootCalculator = std::function<task::Task<crypto::HashType>(Storage&, uint32_t)>;

    EthereumBlockVerifier(Scheduler& scheduler, Executor& executor,
        protocol::BlockFactory& blockFactory)
      : m_scheduler(scheduler), m_executor(executor), m_blockFactory(blockFactory)
    {}

    /// MPT state root over the executed view's Ethereum world state, built incrementally
    /// from the parent block's state root. Forwards to the shared implementation
    /// (ledger::mpt::computeMptStateRoot, bcos-ledger/mpt/StateRoots.h) that the Engine API
    /// block builder also uses.
    template <class ViewType>
    static task::Task<crypto::HashType> computeMptStateRoot(ViewType& view,
        crypto::HashType const& parentStateRoot, ledger::LedgerConfig const& ledgerConfig)
    {
        return ledger::mpt::computeMptStateRoot(view, parentStateRoot, ledgerConfig);
    }
    /// Execute `ethHeader` (child of `parentHeader`) with the given raw EIP-2718
    /// transactions, verify the deterministic roots + state root against the header,
    /// and on success commit block/state/ledger rows atomically.
    ///
    /// @tparam GlobalStateStorage MultiLayerStorage-like: fork()/pushView()/mergeBackStorage()
    /// @param rawUncles raw uncle-header RLP elements (PoW blocks only; empty on PoS)
    /// @param mergeBlock first PoS (merge) block number; blocks below it are PoW and
    ///        receive coinbase/uncle rewards, blocks at or above it pay none
    /// @param stateRootCalculator computes the block's state root over the executed view
    template <class GlobalStateStorage>
    task::Task<EthereumBlockVerificationResult> verifyAndCommit(GlobalStateStorage& globalStateStorage,
        ledger::LedgerInterface& ledger, protocol::EthBlockHeaderData const& ethHeader,
        protocol::EthBlockHeaderData const& parentHeader,
        std::vector<bcos::bytes> const& rawTransactions,
        std::optional<std::vector<bcos::bytes>> const& rawWithdrawals,
        EvmcForkTimestamps const& forkSchedule, uint64_t chainId,
        std::vector<bcos::bytes> const& rawUncles, uint64_t mergeBlock,
        TransactionDecoder const& decoder,
        StateRootCalculator<typename GlobalStateStorage::ViewType> const& stateRootCalculator)
    {
        EthereumBlockVerificationResult result;
        auto fail = [&](std::string message) -> task::Task<EthereumBlockVerificationResult> {
            result.valid = false;
            result.error = std::move(message);
            co_return std::move(result);
        };

        // 1. Ledger config (system config) + per-block EVM overlay.
        ledger::LedgerConfig ledgerConfig;
        auto view = globalStateStorage.fork();
        view.newMutable();
        co_await ledger::getLedgerConfig(
            view, ledgerConfig, ethHeader.number - 1, m_blockFactory.get());
        fillExecutionLedgerConfig(ethHeader, ledgerConfig, forkSchedule, chainId);

        // 2. Execution header carrying the block context for the EVM.
        auto blockHeader = makeExecutionBlockHeader(
            ethHeader, m_blockFactory.get(), ledgerConfig.compatibilityVersion());
        result.header = blockHeader;

        // 2a. The block's EVM revision, resolved once for every fork-gated step below
        //     (system calls, withdrawals).
        auto revOpt = ledgerConfig.evmcRevisionForBlock(ethHeader.number);
        if (!revOpt)
        {
            co_return co_await fail(
                "EthereumBlockVerifier: no EVMC revision configured for the block");
        }
        const evmc_revision blockRevision = *revOpt;

        // 2b. Cancun+ block-start system calls (EIP-4788 beacon roots; EIP-2935
        //     historical block hashes from Prague). geth runs these BEFORE the block's
        //     transactions, so the write must land in the view before executeBlock;
        //     from Cancun on the beacon-roots contract storage is part of every
        //     block's state root, so skipping this makes every Cancun+ stateRoot
        //     verification fail. The contract code is already in state (deployed by
        //     ordinary pre-fork transactions — see EthereumSystemCalls.h); evmone
        //     silently skips a code-less contract, per the EIPs. evmone gates each
        //     contract by revision internally, so a Shanghai block (rev < CANCUN)
        //     must not even call in — the gate below keeps pre-Cancun behavior
        //     byte-identical to before this change.
        if (blockRevision >= EVMC_CANCUN)
        {
            if (auto error = applyBlockStartSystemCalls(
                    view, m_executor.get().vm(), ethHeader, blockRevision);
                error.has_value())
            {
                co_return co_await fail("EthereumBlockVerifier: " + *error);
            }
        }

        // 3. Decode every raw transaction. A failure is a hard invalid: a real block has
        //    exactly one receipt per transaction, so we cannot skip any.
        std::vector<protocol::Transaction::Ptr> transactions;
        transactions.reserve(rawTransactions.size());
        std::exception_ptr decodeFailure;
        std::string decodeDiag;
        for (auto const& raw : rawTransactions)
        {
            try
            {
                auto tx = decoder(raw);
                if (!tx)
                {
                    co_return co_await fail(
                        "EthereumBlockVerifier: decoder returned null for a transaction");
                }
                transactions.push_back(std::move(tx));
            }
            catch (...)
            {
                // Capture the diagnostic INSIDE the catch block: an exception is only
                // "active" there, and current_exception_diagnostic_information rethrows
                // internally (rethrow on no active exception => terminate).
                decodeFailure = std::current_exception();
                try
                {
                    std::rethrow_exception(decodeFailure);
                }
                catch (std::exception const& e)
                {
                    decodeDiag = e.what();
                }
                catch (...)
                {
                    decodeDiag = boost::current_exception_diagnostic_information();
                }
                break;
            }
        }
        if (decodeFailure)
        {
            // Surface the underlying decode error (which transaction, what failed)
            // so a new EIP-2718 type or malformed raw cannot hide behind a generic
            // message.
            auto failedIndex = transactions.size();
            auto const& badRaw = rawTransactions[failedIndex];
            co_return co_await fail(
                "EthereumBlockVerifier: transaction decode failed at index " +
                std::to_string(failedIndex) + " of " + std::to_string(rawTransactions.size()) +
                " (malformed EIP-2718 or unsupported type): " + decodeDiag +
                " raw=" + bcos::toHexStringWithPrefix(bytesConstRef(badRaw.data(), badRaw.size()))
                      .substr(0, 400));
        }
        result.transactions = transactions;

        // 4. Execute the block.
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        std::exception_ptr executeFailure;
        try
        {
            receipts = co_await m_scheduler.get().executeBlock(view, m_executor.get(), *blockHeader,
                transactions | ::ranges::views::indirect, ledgerConfig);
        }
        catch (...)
        {
            executeFailure = std::current_exception();
        }
        if (executeFailure)
        {
            co_return co_await fail("EthereumBlockVerifier: execution failed");
        }
        result.receipts = receipts;

        // 4a. Finalize the block (EIP-4895 withdrawals, Shanghai+). geth applies the
        //     CL's validator withdrawals to the recipients' balances AFTER the block's
        //     transactions; this credit is part of the world state, so it must land in
        //     the view before the MPT state root is computed. PoS blocks pay no block
        //     reward here (pre-merge rewards are handled by accumulatePoWBlockRewards
        //     below), so blockReward is always nullopt on this path. This was missing
        //     entirely, which forked the first post-Shanghai block with withdrawals
        //     (Sepolia block 2990908): the recipients' balances stayed short and the
        //     computed state root diverged from the header's.
        if (rawWithdrawals && !rawWithdrawals->empty())
        {
            std::vector<executor_v1::eth::EthWithdrawal> withdrawals;
            withdrawals.reserve(rawWithdrawals->size());
            for (auto const& raw : *rawWithdrawals)
            {
                protocol::EthWithdrawal wd;
                if (auto err = wd.rlpDecode(bcos::bytesConstRef(raw.data(), raw.size())))
                {
                    co_return co_await fail("EthereumBlockVerifier: withdrawal RLP decode failed");
                }
                auto const& d = wd.data();
                executor_v1::eth::EthWithdrawal ew;
                ew.index = d.index;
                ew.validator_index = d.validatorIndex;
                std::copy_n(d.address.begin(), sizeof(evmc_address), ew.recipient.bytes);
                ew.amount_in_gwei = d.amount;
                withdrawals.push_back(std::move(ew));
            }
            co_await m_executor.get().finalizeBlock(
                view, *blockHeader, ledgerConfig, blockRevision, std::nullopt, withdrawals);
        }

        // 4b. PoW (pre-merge) blocks pay the coinbase block reward (2 ETH) plus
        //     uncle rewards, exactly like geth's accumulateRewards. PoS blocks
        //     (at/above mergeBlock) pay none. This mutates the executed view, so
        //     it must happen before the state root is computed. mergeBlock == 0
        //     means the chain has no PoW phase (pure PoS): no rewards are paid.
        //
        //     TTD caveat: on merge chains (Sepolia), the terminal blocks AFTER
        //     the Terminal Total Difficulty has been reached carry difficulty 0
        //     while still being pre-merge. geth stops calling accumulateRewards
        //     once the TTD is reached (no more mining), so those zero-difficulty
        //     pre-merge blocks pay NO block reward. We gate on difficulty != 0
        //     to match — a nonzero-difficulty pre-merge block is always mined.
        //
        // NOTE: computeMptStateRoot is an INCREMENTAL build that writes MPT nodes
        // into the view, so it must be called EXACTLY ONCE per block (in step 6).
        if (mergeBlock > 0 && static_cast<uint64_t>(ethHeader.number) < mergeBlock &&
            ethHeader.difficulty != 0)
        {
            // A malformed uncle throws (its reward is part of the world state, so it
            // cannot be skipped) — the block is invalid, not partially rewarded.
            std::exception_ptr rewardsFailure;
            try
            {
                co_await accumulatePoWBlockRewards(view, ethHeader, rawUncles, ledgerConfig);
            }
            catch (...)
            {
                rewardsFailure = std::current_exception();
            }
            if (rewardsFailure)
            {
                co_return co_await fail(
                    "EthereumBlockVerifier: uncle header RLP decode failed");
            }
        }

        // 4c. Prague+ block-end system calls (EIP-7002 withdrawal requests, EIP-7251
        //     consolidation requests). geth runs these AFTER the block's transactions
        //     and withdrawals, so the call must land after 4a/4b and before the state
        //     root is computed: the contracts' storage updates are part of the block's
        //     world state. evmone gates by revision internally; the explicit PRAGUE
        //     gate keeps Cancun/Shanghai behavior identical to before this change.
        //
        //     LEFTOVER (EIP-7685 requestsHash): the returned requests are not yet
        //     cross-checked against ethHeader.requestsHash. A complete check also
        //     needs the EIP-6110 deposit requests collected from the receipts, whose
        //     deposit-contract address is per-chain (bcos-evm's requests.cpp pins the
        //     mainnet address) — that plumbing is out of scope for this fix.
        if (blockRevision >= EVMC_PRAGUE)
        {
            auto blockEnd = applyBlockEndSystemCalls(
                view, m_executor.get().vm(), ethHeader, blockRevision);
            if (blockEnd.error.has_value())
            {
                co_return co_await fail("EthereumBlockVerifier: " + *blockEnd.error);
            }
        }

        // 5. Fill cumulativeGasUsed + logsBloom (v2) and compute the deterministic roots.
        auto computation = co_await computeEthereumRoots(
            receipts, transactions | ::ranges::views::indirect, rawTransactions);
        result.computation = computation;

        // 6. State root over the executed view. v2: the Ethereum world-state MPT root
        //    (accounts + storage, incrementally from the parent root); v1: the injected
        //    legacy fold.
        crypto::HashType stateRoot;
        if (ledgerConfig.executorVersion() >= ledger::ETHEREUM_EXECUTOR_VERSION)
        {
            stateRoot =
                co_await computeMptStateRoot(view, parentHeader.stateRoot, ledgerConfig);
        }
        else
        {
            stateRoot = co_await stateRootCalculator(view, blockHeader->version());
        }
        result.stateRoot = stateRoot;

        // 7. Verify against the header.
        if (auto error =
                verifyAgainstHeader(ethHeader, computation, result.stateRoot, rawWithdrawals,
                    rawUncles, transactions);
            error.has_value())
        {
            co_return co_await fail(std::move(*error));
        }

        // 8. Commit: FIB-104 pattern — push the executed view, then merge the ledger
        //    prewrite buffer atomically. If anything after the push throws, roll the
        //    pushed view back (popFrontStorage) so a dirty layer never stays on the
        //    storage stack to pollute later sync rounds — the same rollback pattern
        //    BaselineScheduler::coExecuteBlock uses around pushView.
        globalStateStorage.pushView(std::move(view));
        try
        {
            typename GlobalStateStorage::MutableStorage prewriteStorage;
            auto block = m_blockFactory.get().createBlock();
            // The execution header carries no Tars dataHash; prewriteBlockToBuffer
            // needs header->hash() to write SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER.
            // For an Ethereum header the canonical block hash is keccak256(rlp(header)),
            // which is exactly the value verified against the chain — inject it so the
            // ledger metadata (and the resume point) are written with the real hash.
            //
            // NOTE: BlockImpl::setBlockHeader COPIES the header's inner data, so the
            // RLP hash must be set on blockHeader BEFORE it is copied into the block.
            blockHeader->setRLPHash(bcos::protocol::ethHeaderHash(ethHeader));
            block->setBlockHeader(blockHeader);
            auto const& bloom = computation.logsBloom;
            block->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
            for (auto const& tx : transactions)
            {
                block->appendTransaction(tx);
            }
            for (auto const& receipt : receipts)
            {
                block->appendReceipt(receipt);
            }
            auto blockTxs = std::make_shared<protocol::ConstTransactions>(
                transactions | ::ranges::views::transform([](auto const& transaction) {
                    return protocol::Transaction::ConstPtr(transaction);
                }) | ::ranges::to<std::vector>());
            co_await ledger::prewriteBlockToBuffer(ledger, blockTxs, block, prewriteStorage);
            co_await globalStateStorage.mergeBackStorage(prewriteStorage);
        }
        catch (...)
        {
            globalStateStorage.popFrontStorage();
            throw;
        }

        result.valid = true;
        co_return std::move(result);
    }

    /// Execute the block and compute the deterministic roots, without committing.
    /// Exposed for the Engine API external-payload path that needs the computation
    /// before deciding to commit. Forwards to the shared implementation
    /// (ledger::mpt::computeEthereumRoots, bcos-ledger/mpt/EthereumBlockRoots.h) that the
    /// Engine API block builder (buildPayload) also uses.
    static task::Task<EthereumBlockComputation> computeEthereumRoots(
        std::vector<protocol::TransactionReceipt::Ptr>& receipts,
        ::ranges::input_range auto const& transactions,
        ::ranges::input_range auto const& rawTransactions)
    {
        return ledger::mpt::computeEthereumRoots(receipts, transactions, rawTransactions);
    }

    /// Compare the computed values against the external header's commitments.
    static std::optional<std::string> verifyAgainstHeader(
        protocol::EthBlockHeaderData const& ethHeader, EthereumBlockComputation const& computation,
        crypto::HashType const& stateRoot,
        std::optional<std::vector<bcos::bytes>> const& rawWithdrawals,
        std::vector<bcos::bytes> const& rawUncles,
        std::vector<protocol::Transaction::Ptr> const& transactions)
    {
        if (computation.txsRoot != ethHeader.txsRoot)
        {
            return "transactionsRoot mismatch";
        }
        if (computation.receiptsRoot != ethHeader.receiptsRoot)
        {
            return "receiptsRoot mismatch (computed=" + computation.receiptsRoot.hex() +
                   " header=" + ethHeader.receiptsRoot.hex() + " gasUsed=" +
                   computation.gasUsed.str() + "/" + ethHeader.gasUsed.str() +
                   " logsBloom=" + toHex(computation.logsBloom).substr(0, 64) + "/" +
                   toHex(ethHeader.logsBloom).substr(0, 64) + ")";
        }
        if (computation.gasUsed != ethHeader.gasUsed)
        {
            return "gasUsed mismatch (computed=" + computation.gasUsed.str() +
                   " header=" + ethHeader.gasUsed.str() + ")";
        }
        if (computation.logsBloom != ethHeader.logsBloom)
        {
            return "logsBloom mismatch (computed=" + toHex(computation.logsBloom).substr(0, 64) +
                   " header=" + toHex(ethHeader.logsBloom).substr(0, 64) + ")";
        }
        if (stateRoot != ethHeader.stateRoot)
        {
            return "stateRoot mismatch (computed=" + stateRoot.hex() +
                   " header=" + ethHeader.stateRoot.hex() + ")";
        }
        // Uncle (ommers) hash: keccak256(rlp(uncles)) must match the header's uncleHash.
        // An empty uncle list hashes to the canonical empty-ommers hash
        // (protocol::c_emptyOmmersHash), which is exactly what every PoS header carries
        // — so this one comparison also rejects a PoS block that attaches uncles,
        // without a separate rule.
        if (calculateUnclesHash(rawUncles) != ethHeader.uncleHash)
        {
            return "uncleHash mismatch (computed over " + std::to_string(rawUncles.size()) +
                   " uncles, header=" + ethHeader.uncleHash.hex() + ")";
        }
        // EIP-4844 blob gas commitment (Cancun+): blobGasUsed must equal
        // GAS_PER_BLOB (131072) times the block's total blob count. Count BLOB
        // HASHES, not transactions — one type-3 transaction can carry several
        // blobs (geth recomputes the same value in block.BlobGasUsed()).
        u256 computedBlobGas = 0;
        for (auto const& transaction : transactions)
        {
            computedBlobGas += kBlobGasPerBlob * transaction->blobVersionedHashes().size();
        }
        if (ethHeader.blobGasUsed.has_value())
        {
            if (*ethHeader.blobGasUsed != computedBlobGas)
            {
                return "blobGasUsed mismatch (computed=" + computedBlobGas.str() +
                       " from blob transactions, header=" + ethHeader.blobGasUsed->str() + ")";
            }
        }
        else if (computedBlobGas != 0)
        {
            return "block carries blob transactions but the header has no blobGasUsed";
        }
        if (ethHeader.withdrawalsHash.has_value())
        {
            if (!rawWithdrawals)
            {
                return "withdrawalsHash present but the block carries no withdrawals";
            }
            std::vector<bcos::bytesConstRef> refs;
            refs.reserve(rawWithdrawals->size());
            for (auto const& withdrawal : *rawWithdrawals)
            {
                refs.emplace_back(bcos::ref(withdrawal));
            }
            if (ledger::mpt::calculateWithdrawalsRoot(refs) != *ethHeader.withdrawalsHash)
            {
                return "withdrawalsRoot mismatch";
            }
        }
        else if (rawWithdrawals && !rawWithdrawals->empty())
        {
            return "block carries withdrawals but the header has no withdrawalsHash";
        }
        return std::nullopt;
    }

private:
    std::reference_wrapper<Scheduler> m_scheduler;
    std::reference_wrapper<Executor> m_executor;
    std::reference_wrapper<protocol::BlockFactory> m_blockFactory;
};

}  // namespace bcos::scheduler_v1
