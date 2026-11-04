/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
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
 * @file EESTProductionChain.h
 * @brief An in-process L1 chain for the EEST runner: the PRODUCTION block pipeline
 *        (scheduler_v1::EthereumBlockVerifier — the same executeEthereumBlock the Engine
 *        API, devp2p sync and --import-blocks run) over an in-memory MultiLayerStorage,
 *        seeded from a fixture's `pre` the way a node seeds its genesis (flat accounts plus
 *        the world-state MPT, ledger::importEthereumGenesisState). Blockchain fixtures feed
 *        their RLP blocks to verifyAndCommit; state tests execute one synthetic block and
 *        compare the resulting MPT root with the fixture's post hash.
 */
#pragma once

#include "bcos-crypto/hash/Keccak256.h"

#include "EESTRunner.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/GenesisStateLoader.h"
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/mpt/Constants.h"
#include "bcos-ledger/mpt/FlatToMPT.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-rlp-protocol/EthBlockBody.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthWithdrawal.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-rpc/web3jsonrpc/utils/EngineHelper.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "engine/bcos-engine/EthEngineService.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/EthereumState.h"
#include "libinitializer/EthereumBlockHashLookup.h"
#include "libinitializer/ExternalPayloadVerifier.h"
#include <json/json.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace bcos::test::pipeline
{

// ---- storage: the same shape the node's EL lane runs on, in memory -----------------------

using MutableLayer =
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
/// CONCURRENT because MultiLayerStorage::mergeIntoBackends only merges into that form, but
/// constructed with ONE bucket (see Chain::m_backend): a concurrent MemoryStorage hashes keys
/// into buckets and its RANGE_SEEK iterator only seeks the first one (MemoryStorage.h
/// Iterator::seek, "need !withConcurrent"), so a multi-bucket seek yields keys below the
/// seek point from the other buckets. The executor's clearAccountStorage stops at the first
/// foreign table it sees and would then leave a destructed account's rows behind — a
/// harness artifact a node never hits (its backend is RocksDB, one ordered keyspace).
using Backend =
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::CONCURRENT),
        std::hash<executor_v1::StateKey>>;

/// MultiLayerStorage's checkpoint seam over a plain backend: no checkpoints, one backend.
struct TrivialCheckpoint
{
    using CheckpointName = h256;
    Backend& m_storage;
    explicit TrivialCheckpoint(Backend& storage) : m_storage(storage) {}
    Backend& open() { return m_storage; }
    [[noreturn]] Backend& open(CheckpointName const& /*name*/) { std::abort(); }
    void createCheckpoint(Backend& /*storage*/, CheckpointName const& /*name*/) {}
    void deleteCheckpoint(CheckpointName const& /*name*/) {}
    std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }
    std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }
};

using Storage = storage2::MultiLayerStorage<MutableLayer, void, TrivialCheckpoint>;
using ViewType = Storage::ViewType;
using Verifier = scheduler_v1::EthereumBlockVerifier<scheduler_v1::SchedulerSerialImpl,
    executor_v1::eth::EthereumExecutor>;

// ---- fork schedule from a fixture network name --------------------------------------------

/// The fork ladder in activation order, lower-cased as EEST spells them (aliases below).
inline std::vector<std::string> const& forkLadder()
{
    static const std::vector<std::string> ladder{"frontier", "homestead", "tangerinewhistle",
        "spuriousdragon", "byzantium", "constantinople", "constantinoplefix", "istanbul", "berlin",
        "london", "paris", "shanghai", "cancun", "prague", "osaka", "bpo1", "bpo2", "bpo3", "bpo4",
        "bpo5"};
    return ladder;
}

inline int forkIndex(std::string name)
{
    boost::algorithm::to_lower(name);
    boost::algorithm::erase_all(name, " ");
    if (name == "eip150")
    {
        name = "tangerinewhistle";
    }
    else if (name == "eip158")
    {
        name = "spuriousdragon";
    }
    else if (name == "petersburg")
    {
        name = "constantinoplefix";
    }
    else if (name == "muirglacier")
    {
        name = "istanbul";
    }
    else if (name == "merge")
    {
        name = "paris";
    }
    auto const& ladder = forkLadder();
    auto it = std::find(ladder.begin(), ladder.end(), name);
    return it == ladder.end() ? -1 : static_cast<int>(it - ladder.begin());
}

struct FixtureSchedule
{
    scheduler_v1::EvmcForkTimestamps forks;
    /// verifyAndCommit's merge block: 0 = PoS from genesis, max = the whole fixture is PoW.
    uint64_t mergeBlock = 0;
    /// Forks the schedule cannot express (BPO3+ have no blob-schedule slot yet).
    std::vector<std::string> unsupported;
};

/// "Cancun" = every fork up to Cancun active from genesis; "ShanghaiToCancunAtTime15k" =
/// up to Shanghai at genesis, Cancun at t=15000 s. Unknown names throw.
inline FixtureSchedule scheduleForNetwork(std::string const& network)
{
    FixtureSchedule schedule;
    std::string base = network;
    std::optional<std::string> target;
    uint64_t transition = 0;
    auto const toPos = network.find("To");
    auto const atPos = network.find("AtTime");
    if (toPos != std::string::npos && atPos != std::string::npos && toPos < atPos)
    {
        base = network.substr(0, toPos);
        target = network.substr(toPos + 2, atPos - toPos - 2);
        auto timeStr = network.substr(atPos + 6);
        if (!timeStr.empty() && (timeStr.back() == 'k' || timeStr.back() == 'K'))
        {
            transition = std::stoull(timeStr.substr(0, timeStr.size() - 1)) * 1000;
        }
        else
        {
            transition = std::stoull(timeStr);
        }
    }
    auto const baseIdx = forkIndex(base);
    if (baseIdx < 0)
    {
        throw std::runtime_error("unknown fork in network name: " + network);
    }
    int targetIdx = baseIdx;
    if (target)
    {
        targetIdx = forkIndex(*target);
        if (targetIdx < 0)
        {
            throw std::runtime_error("unknown fork in network name: " + network);
        }
    }

    auto& f = schedule.forks;
    auto timeFor = [&](int idx) -> std::optional<uint64_t> {
        if (idx <= baseIdx)
        {
            return 0;
        }
        if (idx <= targetIdx)
        {
            return transition;
        }
        return std::nullopt;
    };
    auto set = [&](int idx, uint64_t& field) {
        if (auto t = timeFor(idx))
        {
            field = *t;
        }
    };
    set(forkIndex("frontier"), f.frontierTime);
    set(forkIndex("homestead"), f.homesteadTime);
    set(forkIndex("tangerinewhistle"), f.tangerineWhistleTime);
    set(forkIndex("spuriousdragon"), f.spuriousDragonTime);
    set(forkIndex("byzantium"), f.byzantiumTime);
    set(forkIndex("constantinople"), f.constantinopleTime);
    set(forkIndex("constantinoplefix"), f.petersburgTime);
    set(forkIndex("istanbul"), f.istanbulTime);
    set(forkIndex("berlin"), f.berlinTime);
    set(forkIndex("london"), f.londonTime);
    set(forkIndex("paris"), f.parisTime);
    set(forkIndex("shanghai"), f.shanghaiTime);
    set(forkIndex("cancun"), f.cancunTime);
    set(forkIndex("prague"), f.pragueTime);
    set(forkIndex("osaka"), f.osakaTime);
    set(forkIndex("bpo1"), f.bpo1Time);
    set(forkIndex("bpo2"), f.bpo2Time);
    for (auto const* name : {"bpo3", "bpo4", "bpo5"})
    {
        if (timeFor(forkIndex(name)).has_value())
        {
            schedule.unsupported.emplace_back(name);
        }
    }
    // The fixture is PoW throughout when Paris never activates; a transition into Paris by
    // timestamp does not occur in the fixture set (the merge is a TTD event).
    schedule.mergeBlock = f.parisTime == scheduler_v1::EvmcForkTimestamps::kForkDisabled ?
                              std::numeric_limits<uint64_t>::max() :
                              0;
    return schedule;
}

// ---- fixture pre-state -> genesis allocs --------------------------------------------------

inline std::string stripHex(std::string const& hex)
{
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
    {
        return hex.substr(2);
    }
    return hex;
}

inline std::string padHex64(std::string const& hex)
{
    auto digits = stripHex(hex);
    if (digits.size() < 64)
    {
        digits.insert(0, 64 - digits.size(), '0');
    }
    return digits;
}

inline std::vector<ledger::Alloc> allocsFromPre(std::map<std::string, EESTAccount> const& pre)
{
    std::vector<ledger::Alloc> allocs;
    allocs.reserve(pre.size());
    for (auto const& [address, account] : pre)
    {
        ledger::Alloc alloc;
        alloc.address = stripHex(address);
        alloc.balance = account.balance.empty() ? u256(0) : hexToU256(account.balance);
        alloc.nonce =
            (account.nonce.empty() ? u256(0) : hexToU256(account.nonce)).str(0, std::ios_base::dec);
        alloc.code = stripHex(account.code);
        for (auto const& [key, value] : account.storage)
        {
            alloc.storage.emplace_back(padHex64(key), padHex64(value));
        }
        allocs.push_back(std::move(alloc));
    }
    return allocs;
}

// ---- the chain ------------------------------------------------------------------------------

/// FakeLedger whose prewrite persists the rows Ledger::asyncPrewriteBlock writes and the
/// pipeline reads back (the current height, hash<->number, the header) into the storage it is
/// handed — the commit view — so a committed block is the next block's parent the way it is
/// on a node. FakeLedger itself drops them, which would stall the verifier's height guard at
/// genesis.
class PersistingLedger : public FakeLedger
{
public:
    using FakeLedger::FakeLedger;

    void asyncPrewriteBlock(storage::StorageInterface::Ptr storage,
        protocol::ConstTransactionsPtr /*transactions*/, protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool /*writeTxsAndReceipts*/,
        std::optional<ledger::Features> /*features*/,
        std::optional<crypto::HashType> blockHashOverride, bool /*writeNonces*/) override
    {
        if (block)
        {
            auto header = block->blockHeader();
            auto const blockHash = blockHashOverride.value_or(
                protocol::ethHeaderHash(protocol::EthBlockHeader(*header).data()));
            writeBlockRows(*storage, *header, blockHash);
        }
        callback("", nullptr);
    }

    static void writeBlockRows(
        storage::StorageInterface& storage, protocol::BlockHeader const& header, h256 const& hash)
    {
        auto const numberStr = boost::lexical_cast<std::string>(header.number());
        auto set = [&](std::string_view table, std::string_view key, storage::Entry entry) {
            storage.asyncSetRow(table, key, std::move(entry), [](auto&&) {});
        };
        storage::Entry current;
        current.set(numberStr);
        set(ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER, std::move(current));
        storage::Entry hashToNumber;
        hashToNumber.set(numberStr);
        set(ledger::SYS_HASH_2_NUMBER, concepts::bytebuffer::toView(hash), std::move(hashToNumber));
        storage::Entry numberToHash;
        numberToHash.set(hash.asBytes());
        set(ledger::SYS_NUMBER_2_HASH, numberStr, std::move(numberToHash));
        bytes encoded;
        header.encode(encoded);
        storage::Entry headerRow;
        headerRow.set(std::move(encoded));
        set(ledger::SYS_NUMBER_2_BLOCK_HEADER, numberStr, std::move(headerRow));
    }
};

/// The verifier's verdict on one block. `fault` is not a verdict: the pipeline threw where
/// the verifier turns every decode, execution and reward failure into a refusal itself, so
/// the harness or its storage broke. The runner fails the fixture on a fault whatever the
/// fixture expected, so a fault can never pass as the exception a fixture asks for.
struct BlockOutcome
{
    bool valid = false;
    std::string error{};
    std::optional<std::string> fault{};
};

struct ExecutionOutcome
{
    std::optional<std::string> error;  ///< the verifier's refusal (a verdict)
    std::optional<std::string> fault;  ///< the pipeline threw (see BlockOutcome::fault)
    crypto::HashType stateRoot;
    std::optional<ViewType> view;  ///< the executed (uncommitted) state, for post-state reads
};

/// One fixture's chain: genesis seeded from `pre`, blocks applied through the production
/// verifier. The storage is a fresh in-memory MultiLayerStorage per instance.
class Chain
{
public:
    /// State-test mode: BLOCKHASH answers keccak256(decimal block number) in the window
    /// instead of reading the (nonexistent) ancestors' rows.
    bool stateTestBlockHashes = false;

    Chain()
      : m_ioServicePool(std::make_shared<IOServicePool>(1, std::string("eest"))),
        m_scheduler(m_ioServicePool),
        m_cryptoSuite(createNormalCryptoSuite()),
        m_receiptFactory(m_cryptoSuite),
        m_blockFactory(createBlockFactory(m_cryptoSuite)),
        m_executor(m_receiptFactory,
            [this](int64_t blockNumber, int64_t currentHeight) -> evmc::bytes32 {
                if (stateTestBlockHashes)
                {
                    // State tests have no real ancestors: t8n defines BLOCKHASH(n) inside the
                    // 256-block window as keccak256 of n's decimal string (EEST
                    // test_blockhash_state_test_recency pins it); outside the window, zero.
                    if (blockNumber < 0 || blockNumber >= currentHeight ||
                        currentHeight - blockNumber > 256)
                    {
                        return {};
                    }
                    auto const text = std::to_string(blockNumber);
                    auto const hash = crypto::keccak256Hash(bytesConstRef(text));
                    evmc::bytes32 out{};
                    std::copy_n(hash.data(), hash.size(), out.bytes);
                    return out;
                }
                return initializer::ethBlockHashLookupFromStorage(
                    m_backend, blockNumber, currentHeight);
            }),
        m_checkpoint(m_backend),
        m_storage(m_checkpoint),
        m_verifier(std::make_shared<Verifier>(m_scheduler, m_executor, *m_blockFactory,
            /*commitObserver=*/nullptr, /*reorgWindow=*/0)),
        m_ledger(std::make_shared<PersistingLedger>())
    {}

    Chain(Chain const&) = delete;
    Chain& operator=(Chain const&) = delete;

    crypto::Hash& hashImpl() { return *m_cryptoSuite->hashImpl(); }
    protocol::EthBlockHeaderData const& parent() const { return m_parent; }
    Storage& storage() { return m_storage; }
    std::shared_ptr<Verifier> const& verifier() const { return m_verifier; }
    std::shared_ptr<PersistingLedger> const& ledger() const { return m_ledger; }
    protocol::BlockFactory::Ptr const& blockFactory() const { return m_blockFactory; }

    /// Write the fixture's `pre` as the genesis world state — flat accounts plus every MPT
    /// node, exactly as a node's genesis import does — and the genesis header rows the
    /// pipeline reads (current number, hash<->number, the header itself). Returns the state
    /// root the trie builder computed; the caller compares it with the fixture's.
    h256 seedGenesis(std::map<std::string, EESTAccount> const& pre,
        protocol::EthBlockHeaderData genesisHeader, scheduler_v1::EvmcForkTimestamps const& forks)
    {
        auto const root = task::syncWait(
            ledger::importEthereumGenesisState(m_backend, allocsFromPre(pre), hashImpl()));
        if (genesisHeader.stateRoot == h256{})
        {
            // A synthetic parent (state tests): its root is whatever the pre-state hashes to.
            genesisHeader.stateRoot = root;
        }
        writeSysConfig(magic_enum::enum_name(ledger::SystemConfig::executor_version),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        writeSysConfig(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit),
            genesisHeader.gasLimit.str(0, std::ios_base::dec));
        auto header = scheduler_v1::makeExecutionBlockHeader(genesisHeader, *m_blockFactory,
            static_cast<uint32_t>(protocol::DEFAULT_VERSION), forks);
        auto backend = std::make_shared<storage::LegacyStorageWrapper<Backend>>(m_backend);
        PersistingLedger::writeBlockRows(*backend, *header, protocol::ethHeaderHash(genesisHeader));
        m_parent = genesisHeader;
        return root;
    }

    /// Verify and commit one RLP block through the production verifier. On success the block
    /// becomes the parent of the next one; its rows were written by the commit itself
    /// (PersistingLedger).
    BlockOutcome applyBlock(
        protocol::EthBlockData const& block, FixtureSchedule const& schedule, uint64_t chainId)
    {
        std::vector<bytes> rawUncles;
        rawUncles.reserve(block.ommers.size());
        for (auto const& ommer : block.ommers)
        {
            bytes encoded;
            codec::rlp::encode(encoded, ommer);
            rawUncles.push_back(std::move(encoded));
        }
        std::optional<std::vector<bytes>> rawWithdrawals;
        if (block.withdrawals.has_value())
        {
            rawWithdrawals.emplace();
            rawWithdrawals->reserve(block.withdrawals->size());
            for (auto const& withdrawal : *block.withdrawals)
            {
                bytes encoded;
                codec::rlp::encode(encoded, withdrawal);
                rawWithdrawals->push_back(std::move(encoded));
            }
        }
        scheduler_v1::EthereumBlockVerificationResult result;
        try
        {
            result = task::syncWait(m_verifier->verifyAndCommit(m_storage, *m_ledger, block.header,
                m_parent, block.transactions, rawWithdrawals, schedule.forks, chainId, rawUncles,
                schedule.mergeBlock, decoder(), stateRootCalculator()));
        }
        catch (std::exception const& e)
        {
            return BlockOutcome{.valid = false, .fault = std::string(e.what())};
        }
        if (result.valid)
        {
            m_parent = block.header;
        }
        return BlockOutcome{.valid = result.valid, .error = result.error};
    }

    /// Execute one block over a fork of the current state WITHOUT committing (state tests):
    /// the outcome carries the executed view for post-state reads and the MPT root.
    ExecutionOutcome executeOnly(protocol::EthBlockHeaderData const& header,
        protocol::EthBlockHeaderData const& parentHeader, std::vector<bytes> const& rawTransactions,
        std::optional<std::vector<bytes>> const& rawWithdrawals, FixtureSchedule const& schedule,
        uint64_t chainId, Verifier::BlockExecutionOptions options)
    {
        ExecutionOutcome outcome;
        outcome.view.emplace(m_storage.fork());
        outcome.view->newMutable();
        try
        {
            auto execution = task::syncWait(m_verifier->executeEthereumBlock(*outcome.view, header,
                parentHeader, rawTransactions, rawWithdrawals, schedule.forks, chainId,
                /*rawUncles=*/{}, schedule.mergeBlock, decoder(), stateRootCalculator(), options));
            outcome.error = std::move(execution.error);
            outcome.stateRoot = execution.stateRoot;
        }
        catch (std::exception const& e)
        {
            outcome.fault = std::string(e.what());
        }
        return outcome;
    }

    /// A read view over the committed chain state.
    ViewType committedView() { return m_storage.fork(); }

private:
    Verifier::TransactionDecoder decoder()
    {
        return [this](bytes const& raw) -> protocol::Transaction::Ptr {
            return bcos::rpc::decodeWeb3RawTransaction(
                bytesConstRef(raw.data(), raw.size()), hashImpl());
        };
    }

    static Verifier::StateRootCalculator<ViewType> stateRootCalculator()
    {
        return [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };
    }

    void writeSysConfig(std::string_view key, std::string value)
    {
        storage::Entry entry;
        entry.set(storage::serialize::encode(ledger::SystemConfigEntry{std::move(value), 0}));
        task::syncWait(storage2::writeOne(
            m_backend, executor_v1::StateKey{ledger::SYS_CONFIG, key}, std::move(entry)));
    }

    std::shared_ptr<IOServicePool> m_ioServicePool;
    scheduler_v1::SchedulerSerialImpl m_scheduler;
    crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl m_receiptFactory;
    protocol::BlockFactory::Ptr m_blockFactory;
    executor_v1::eth::EthereumExecutor m_executor;
    Backend m_backend{/*buckets=*/1};  // one ordered keyspace, see the Backend alias
    TrivialCheckpoint m_checkpoint;
    Storage m_storage;
    std::shared_ptr<Verifier> m_verifier;
    std::shared_ptr<PersistingLedger> m_ledger;
    protocol::EthBlockHeaderData m_parent;
};

// ---- headers from fixture JSON -------------------------------------------------------------

/// The Ethereum header a fixture's `genesisBlockHeader` object describes (geth field names).
inline protocol::EthBlockHeaderData headerFromJson(Json::Value const& json)
{
    auto hex32 = [&](char const* key) {
        auto raw = hexToBytes(readHexField(json, key));
        h256 out{};
        if (raw.size() <= h256::SIZE)
        {
            std::copy(raw.begin(), raw.end(), out.begin() + (h256::SIZE - raw.size()));
        }
        return out;
    };
    auto has = [&](char const* key) {
        return json.isMember(key) && !json[key].isNull() && !json[key].asString().empty();
    };
    protocol::EthBlockHeaderData header;
    header.number = hexToInt64(readRequiredHex(json, "number"));
    header.timestamp = hexToTimestamp(readRequiredHex(json, "timestamp"));
    header.gasLimit = hexToU256(readRequiredHex(json, "gasLimit"));
    header.gasUsed =
        hexToU256(readHexField(json, "gasUsed").empty() ? "0x0" : readHexField(json, "gasUsed"));
    header.difficulty = has("difficulty") ? hexToU256(readHexField(json, "difficulty")) : u256(0);
    header.parentInfo =
        protocol::ParentInfo{.blockNumber = header.number - 1, .blockHash = hex32("parentHash")};
    header.uncleHash = hex32("uncleHash");
    header.stateRoot = hex32("stateRoot");
    header.txsRoot = hex32("transactionsTrie");
    header.receiptsRoot = hex32("receiptTrie");
    header.prevRandao = hex32("mixHash");
    {
        auto raw = hexToBytes(readHexField(json, "coinbase"));
        if (raw.size() == bcos::Address::SIZE)
        {
            std::copy(raw.begin(), raw.end(), header.coinbase.begin());
        }
    }
    {
        auto raw = hexToBytes(readHexField(json, "bloom"));
        if (raw.size() == header.logsBloom.size())
        {
            std::copy(raw.begin(), raw.end(), header.logsBloom.begin());
        }
    }
    {
        auto raw = hexToBytes(readHexField(json, "nonce"));
        if (raw.size() == h64::SIZE)
        {
            std::copy(raw.begin(), raw.end(), header.nonce.begin());
        }
    }
    header.extraData = hexToBytes(readHexField(json, "extraData"));
    if (has("baseFeePerGas"))
    {
        header.baseFee = hexToU256(readHexField(json, "baseFeePerGas"));
    }
    if (has("withdrawalsRoot"))
    {
        header.withdrawalsHash = hex32("withdrawalsRoot");
    }
    if (has("blobGasUsed"))
    {
        header.blobGasUsed = hexToU256(readHexField(json, "blobGasUsed"));
    }
    if (has("excessBlobGas"))
    {
        header.excessBlobGas = hexToU256(readHexField(json, "excessBlobGas"));
    }
    if (has("parentBeaconBlockRoot"))
    {
        header.parentBeaconRoot = hex32("parentBeaconBlockRoot");
    }
    if (has("requestsHash"))
    {
        header.requestsHash = hex32("requestsHash");
    }
    return header;
}

// ---- the Engine API over the chain ---------------------------------------------------------

/// The v1 (FISCO executor) hooks EthEngineService requires but the L1 lane never calls:
/// inert, like the engine unit tests' stubs.
struct NullExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return nullptr; }
    };
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const protocol::BlockHeader&, const protocol::Transaction&, int,
        const ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&, const protocol::BlockHeader&,
        const protocol::Transaction&, int, const ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

struct NullScheduler
{
    template <class Storage, class Executor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage&, Executor&,
        const protocol::BlockHeader&, ::ranges::input_range auto&& transactions,
        const ledger::LedgerConfig&)
    {
        crypto::Keccak256 hasher;
        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        for (auto const& transaction : transactions)
        {
            (void)transaction;
            auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
            receipt->calculateHash(hasher);
            receipts.push_back(std::move(receipt));
        }
        co_return receipts;
    }
};

using EngineService =
    engine::EthEngineService<txpool::MemPoolImpl, Storage, NullExecutor, NullScheduler>;

/// EthEngineService wired over a Chain exactly as libinitializer wires the L1 lane: the
/// ExternalPayloadVerifierImpl seam over the chain's verifier and ledger, an L1 mempool, the
/// production JSON parser for the request params.
class EngineHarness
{
public:
    EngineHarness(Chain& chain, FixtureSchedule const& schedule, uint64_t chainId)
      : m_memPool(txpool::MemPoolConfig{.chainKind = txpool::ChainKind::L1}),
        m_external(std::make_shared<initializer::ExternalPayloadVerifierImpl<Storage>>(
            chain.verifier(), chain.ledger(), chain.blockFactory(), schedule.forks, chainId,
            schedule.mergeBlock)),
        m_service(m_memPool, chain.storage(), m_executor, m_scheduler, chain.blockFactory(),
            /*ledger=*/nullptr, static_cast<std::uint32_t>(engine::ApiVersion::V4),
            /*commitObserver=*/nullptr, /*ledgerConfigState=*/nullptr, m_external,
            std::make_shared<engine::engine_common::ClSyncCoordination>(),
            /*allowBlobTransactions=*/true)
    {}

    engine::NewPayloadRequest parse(Json::Value const& params, int version)
    {
        return bcos::rpc::parseNewPayloadRequest(params, static_cast<engine::ApiVersion>(version));
    }

    engine::PayloadStatus newPayload(Json::Value const& params, int version)
    {
        auto request = parse(params, version);
        return task::syncWait(m_service.newPayload(request, static_cast<std::uint32_t>(version)));
    }

    h256 blockHashOf(Json::Value const& params, int version)
    {
        return parse(params, version).executionPayload.blockHash;
    }

    engine::ForkchoiceUpdatedResult forkchoice(h256 const& head, int version)
    {
        engine::ForkchoiceState state{head, head, head};
        return task::syncWait(
            m_service.updateForkchoice(state, nullptr, static_cast<std::uint32_t>(version)));
    }

private:
    txpool::MemPoolImpl m_memPool;
    NullExecutor m_executor;
    NullScheduler m_scheduler;
    std::shared_ptr<initializer::ExternalPayloadVerifierImpl<Storage>> m_external;
    EngineService m_service;
};

// ---- post-state check over any readable state --------------------------------------------

/// Compare the expected accounts with @p storage (a committed view or an executed one).
/// Returns the mismatches as text, empty when every expected field matches.
template <class StorageT>
std::string verifyPostState(StorageT& storage, std::map<std::string, EESTAccount> const& expected)
{
    std::ostringstream errors;
    for (auto const& [addrHex, expectedAcc] : expected)
    {
        auto addrBytes = hexToBytes(addrHex);
        if (addrBytes.size() != sizeof(evmc_address))
        {
            continue;
        }
        evmc_address addr{};
        std::copy(addrBytes.begin(), addrBytes.end(), addr.bytes);
        auto account = executor_v1::eth::ethViewAccount(storage, addr);
        task::syncWait([&]() -> task::Task<void> {
            if (!expectedAcc.nonce.empty() && expectedAcc.nonce != "0x")
            {
                auto stored = co_await account.nonce();
                auto const expectedNonce = hexToU256(expectedAcc.nonce);
                auto const actualNonce = u256(stored.value_or("0"));
                if (expectedNonce != actualNonce)
                {
                    errors << "    NONCE " << addrHex << ": expected " << expectedNonce << ", got "
                           << actualNonce << "\n";
                    co_return;
                }
            }
            if (!expectedAcc.balance.empty())
            {
                auto const stored = co_await account.balance();
                auto const expectedBalance = hexToU256(expectedAcc.balance);
                if (expectedBalance != stored)
                {
                    errors << "    BALANCE " << addrHex << ": expected " << expectedBalance
                           << ", got " << stored << "\n";
                    co_return;
                }
            }
            if (!expectedAcc.code.empty() && expectedAcc.code != "0x")
            {
                auto codeEntry = co_await account.code();
                auto const expectedCode = hexToBytes(expectedAcc.code);
                bool match = codeEntry.has_value();
                if (match)
                {
                    auto view = codeEntry->get();
                    match = bytes(view.begin(), view.end()) == expectedCode;
                }
                if (!match)
                {
                    errors << "    CODE " << addrHex << ": expected " << expectedAcc.code << "\n";
                    co_return;
                }
            }
            for (auto const& [key, value] : expectedAcc.storage)
            {
                auto toWord = [](std::string const& hex) {
                    auto raw = hexToBytes(hex);
                    evmc_bytes32 word{};
                    if (raw.size() <= 32)
                    {
                        std::copy(raw.begin(), raw.end(), word.bytes + 32 - raw.size());
                    }
                    else
                    {
                        std::copy_n(raw.end() - 32, 32, word.bytes);
                    }
                    return word;
                };
                auto const storageKey = toWord(key);
                auto const stored = co_await account.storage(storageKey);
                auto const expectedWord = toWord(value);
                if (!std::equal(std::begin(stored.bytes), std::end(stored.bytes),
                        std::begin(expectedWord.bytes)))
                {
                    std::string actualHex;
                    boost::algorithm::hex_lower(
                        stored.bytes, stored.bytes + 32, std::back_inserter(actualHex));
                    errors << "    STORAGE " << addrHex << " key=" << key << ": expected " << value
                           << ", got 0x" << actualHex << "\n";
                }
            }
        }());
    }
    return errors.str();
}

// ---- diagnostics: which accounts the executed state holds vs the fixture's ---------------

struct LiveAccount
{
    std::string address;  // 40 hex
    u256 nonce;
    u256 balance;
    h256 codeHash;
};

/// Every account with at least one live (non-deleted) row in @p storage, under either account
/// table layout, with its core fields. Diagnostics only: a full scan of the flat state.
template <class StorageT>
std::vector<LiveAccount> listLiveAccounts(StorageT& storage)
{
    std::vector<LiveAccount> out;
    task::syncWait([&]() -> task::Task<void> {
        for (std::string_view prefix : {std::string_view{"/apps/"}, std::string_view{"/s/"}})
        {
            std::map<std::string, bool> tables;  // table -> any live row
            auto it = co_await storage2::range(
                storage, storage2::RANGE_SEEK, executor_v1::StateKey{prefix, std::string_view{}});
            while (auto kv = co_await it.next())
            {
                auto const& [k, v] = *kv;
                executor_v1::StateKeyView view(k);
                std::string_view table = view.m_table;
                if (!table.starts_with(prefix))
                {
                    break;
                }
                bool live = true;
                if constexpr (requires { std::holds_alternative<storage2::DELETED_TYPE>(v); })
                {
                    live = !std::holds_alternative<storage2::DELETED_TYPE>(v);
                }
                tables[std::string(table)] = tables[std::string(table)] || live;
            }
            for (auto const& [table, live] : tables)
            {
                if (!live)
                {
                    continue;
                }
                std::string address;
                if (prefix == "/apps/")
                {
                    address = table.substr(prefix.size());
                }
                else
                {
                    boost::algorithm::hex_lower(
                        table.begin() + prefix.size(), table.end(), std::back_inserter(address));
                }
                ledger::account::EVMAccount<StorageT> account(
                    storage, ledger::account::FromTableName{}, table);
                ledger::mpt::FlatAccountMeta meta;
                std::string unreadable;
                try
                {
                    meta = co_await ledger::mpt::readFlatAccountMeta(
                        storage, bcos::Address(address), ledger::account::nodeAddressTableMode());
                }
                catch (std::exception const& e)
                {
                    unreadable = e.what();
                }
                if (!unreadable.empty())
                {
                    // Diagnostics must not abort the run: show the raw rows the reader refused.
                    std::cerr << "    UNREADABLE 0x" << address << ": " << unreadable << "\n";
                    auto rows = co_await storage2::range(storage, storage2::RANGE_SEEK,
                        executor_v1::StateKey{std::string_view(table), std::string_view{}});
                    while (auto row = co_await rows.next())
                    {
                        auto const& [rk, rv] = *row;
                        executor_v1::StateKeyView rowView(rk);
                        if (rowView.m_table != table)
                        {
                            break;
                        }
                        bool deleted = false;
                        if constexpr (requires {
                                          std::holds_alternative<storage2::DELETED_TYPE>(rv);
                                      })
                        {
                            deleted = std::holds_alternative<storage2::DELETED_TYPE>(rv);
                        }
                        std::cerr << "      row " << rowView.m_key
                                  << (deleted ? " DELETED" : " live") << "\n";
                    }
                    continue;
                }
                out.push_back(LiveAccount{.address = address,
                    .nonce = meta.nonce,
                    .balance = meta.balance,
                    .codeHash = meta.codeHash});
            }
        }
    }());
    return out;
}

/// The root the fixture's post-state accounts hash to on their own (the trie builder over
/// `state` as if it were a genesis): equal to the fixture hash when `state` is the complete
/// world state, which makes the account diff below authoritative.
inline h256 rootOfAccounts(std::map<std::string, EESTAccount> const& accounts)
{
    ledger::GenesisConfig genesis;
    genesis.m_executorVersion = ledger::ETHEREUM_EXECUTOR_VERSION;
    genesis.m_allocs = allocsFromPre(accounts);
    return task::syncWait(ledger::computeGenesisStateRoot(genesis));
}

/// Text diff of the accounts present in @p storage against @p expected (by address).
template <class StorageT>
std::string diffAccounts(StorageT& storage, std::map<std::string, EESTAccount> const& expected)
{
    std::ostringstream out;
    std::map<std::string, EESTAccount const*> expectedByAddress;
    for (auto const& [address, account] : expected)
    {
        std::string key = stripHex(address);
        boost::algorithm::to_lower(key);
        expectedByAddress[key] = &account;
    }
    std::set<std::string> seen;
    for (auto const& live : listLiveAccounts(storage))
    {
        std::string key = live.address;
        boost::algorithm::to_lower(key);
        seen.insert(key);
        if (!expectedByAddress.contains(key))
        {
            out << "    EXTRA   0x" << key << " nonce=" << live.nonce << " balance=" << live.balance
                << " codeHash=" << live.codeHash.hex() << "\n";
        }
    }
    for (auto const& [key, account] : expectedByAddress)
    {
        if (!seen.contains(key))
        {
            out << "    MISSING 0x" << key << " nonce=" << account->nonce
                << " balance=" << account->balance << " code=" << account->code.substr(0, 18)
                << "\n";
            // Every row under the missing account's tables, deleted or not: tells whether the
            // account was never written or written and then removed.
            task::syncWait([&]() -> task::Task<void> {
                for (std::string table : {"/apps/" + key,
                         std::string("/s/") +
                             std::string(
                                 reinterpret_cast<char const*>(hexToBytes("0x" + key).data()), 20)})
                {
                    auto it = co_await storage2::range(storage, storage2::RANGE_SEEK,
                        executor_v1::StateKey{std::string_view(table), std::string_view{}});
                    while (auto kv = co_await it.next())
                    {
                        auto const& [k, v] = *kv;
                        executor_v1::StateKeyView view(k);
                        if (view.m_table != table)
                        {
                            break;
                        }
                        bool deleted = false;
                        if constexpr (requires {
                                          std::holds_alternative<storage2::DELETED_TYPE>(v);
                                      })
                        {
                            deleted = std::holds_alternative<storage2::DELETED_TYPE>(v);
                        }
                        out << "      row " << view.m_key << (deleted ? " DELETED" : " live")
                            << "\n";
                    }
                }
            }());
        }
    }
    return out.str();
}

}  // namespace bcos::test::pipeline
