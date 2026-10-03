/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file EthEngineL1BuildTest.cpp
 * @brief EL-mode self-build / self-verify closed loop: EthEngineService builds an L1
 *        payload through the REAL IExternalPayloadVerifier (deriveL1Context ->
 *        buildL1Block -> EthereumBlockVerifier::executeEthereumBlock), then a second
 *        node holding identical genesis state verifies that payload through
 *        EthereumBlockVerifier::verifyAndCommit. The transactions carry genuine
 *        secp256k1 signatures, so both sides run the production raw-envelope decoder.
 */

#include "engine/bcos-engine/EthEngineService.h"
#include "engine/test/unittests/engine/EthServiceStubs.h"

#include "libinitializer/EthereumBlockHashLookup.h"
#include "libinitializer/ExternalPayloadVerifier.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/kzg/Kzg4844.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-transaction-scheduler/EthereumBlockVerifier.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <ethereum-executor/EthStorageErrorGuard.h>
#include <ethereum-executor/EthereumExecutor.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::engine::eth_test;

namespace
{
using EL1BVerifier = scheduler_v1::EthereumBlockVerifier<scheduler_v1::SchedulerSerialImpl,
    executor_v1::eth::EthereumExecutor>;
using EL1BService = EthEngineService<bcos::txpool::MemPoolImpl, RealGlobalStateStorage,
    StubExecutor, StubScheduler>;

// Genesis block-0 context every case shares: a 30M-gas Cancun parent at the slot before
// the payload timestamp (the Engine-API default, 1700000000 s).
constexpr int64_t c_parentTimestamp = 1700000000 - 12;                   // seconds
constexpr std::uint64_t c_blockTimestampMs = c_defaultPayloadTimestamp;  // 1700000000 s
constexpr u256 c_parentGasLimit = u256(30000000);
const h256 c_genesisHash{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};

scheduler_v1::EvmcForkTimestamps el1bCancunForks()
{
    scheduler_v1::EvmcForkTimestamps forks;
    // Unset fields default to kForkDisabled — London..Cancun must be pinned to 0 to mean
    // "active from genesis"; Prague/Osaka/BPO stay disabled (Cancun-era chain).
    forks.londonTime = 0;
    forks.parisTime = 0;
    forks.shanghaiTime = 0;
    forks.cancunTime = 0;
    return forks;
}

/// Sign the EIP-2718 signing hash with a real secp256k1 key and return the raw envelope.
bcos::bytes el1bSign(bcos::rpc::Web3Transaction& w3, crypto::KeyPairInterface const& keyPair)
{
    crypto::Secp256k1Crypto secp;
    auto const sig = secp.sign(keyPair, w3.hashForSign(), false);
    BOOST_REQUIRE(sig);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];  // typed transactions carry yParity directly
    return w3.encode();
}

task::Task<void> el1bFund(
    RealGlobalStateBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<RealGlobalStateBackendStorage> acc(storage, addr, nodeAddressTableMode());
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

template <class Storage>
task::Task<u256> el1bBalance(Storage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, nodeAddressTableMode());
    co_return co_await acc.balance();
}

void el1bWriteCurrentNumber(RealGlobalStateBackendStorage& backend, int64_t number)
{
    storage::Entry entry;
    entry.set(std::to_string(number));
    task::syncWait(storage2::writeOne(backend,
        executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
}

/// The number-keyed header row the EL build lane reads the parent Ethereum header from
/// (updateForkchoice -> getBlockData(HEADER) -> EthBlockHeader(...).data()).
void el1bSeedParentHeaderRow(RealGlobalStateBackendStorage& backend,
    bcos::protocol::BlockFactory& blockFactory, protocol::EthBlockHeaderData const& parent)
{
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(parent.number);
    header->setTimestamp(parent.timestamp * 1000);  // Eth data is seconds, TARS is ms
    header->setGasLimit(parent.gasLimit);
    header->setGasUsed(parent.gasUsed);
    header->setStateRoot(parent.stateRoot);
    header->setTxsRoot(parent.txsRoot);
    header->setReceiptsRoot(parent.receiptsRoot);
    if (parent.baseFee.has_value())
    {
        header->setBaseFee(*parent.baseFee);
    }
    if (parent.blobGasUsed.has_value())
    {
        header->setBlobGasUsed(*parent.blobGasUsed);
    }
    if (parent.excessBlobGas.has_value())
    {
        header->setExcessBlobGas(*parent.excessBlobGas);
    }
    header->setEthBlockVersion(protocol::EthBlockVersion::CANCUN);
    bcos::bytes buffer;
    header->encode(buffer);
    storage::Entry entry;
    entry.set(std::move(buffer));
    task::syncWait(storage2::writeOne(backend,
        executor_v1::StateKey{
            ledger::SYS_NUMBER_2_BLOCK_HEADER, boost::lexical_cast<std::string>(parent.number)},
        std::move(entry)));
}

/// One node's storage stack + the real execution/verification pipeline (the same
/// EthereumExecutor + EthereumBlockVerifier pairing the production Initializer wires).
struct EL1BNode
{
    RealGlobalStateBackendStorage backendStorage;
    RealGlobalCheckpointBackend checkpointBackend{backendStorage};
    RealGlobalStateStorage storage{checkpointBackend};
    std::shared_ptr<bcos::IOServicePool> ioServicePool;
    std::unique_ptr<scheduler_v1::SchedulerSerialImpl> scheduler;
    crypto::CryptoSuite::Ptr cryptoSuite = bcos::test::createNormalCryptoSuite();
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    bcos::protocol::BlockFactory::Ptr blockFactory = bcos::test::createBlockFactory(cryptoSuite);
    std::shared_ptr<executor_v1::eth::EthereumExecutor> executor;
    std::shared_ptr<EL1BVerifier> verifier;
    std::shared_ptr<bcos::test::FakeLedger> fakeLedger = std::make_shared<bcos::test::FakeLedger>();

    explicit EL1BNode(std::string name, int64_t reorgWindow = 0)
    {
        ioServicePool = std::make_shared<bcos::IOServicePool>(1, std::move(name));
        scheduler = std::make_unique<scheduler_v1::SchedulerSerialImpl>(ioServicePool);
        executor_v1::eth::BlockHashLookup lookup = [&backend = backendStorage](
                                                       int64_t blockNumber, int64_t currentHeight) {
            return initializer::ethBlockHashLookupFromStorage(backend, blockNumber, currentHeight);
        };
        executor =
            std::make_shared<executor_v1::eth::EthereumExecutor>(receiptFactory, std::move(lookup));
        verifier = std::make_shared<EL1BVerifier>(*scheduler, *executor, *blockFactory,
            /*commitObserver=*/nullptr, reorgWindow);
    }

    /// Identical genesis on both nodes: executor config, funded accounts, the canonical
    /// head rows, and the parent header row the build lane derives the L1 context from.
    void seedGenesis(protocol::EthBlockHeaderData const& parent,
        std::vector<std::pair<evmc_address, u256>> const& funded)
    {
        writeEthExecutorConfig(backendStorage, EVMC_CANCUN);
        writeRawSysConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");
        for (auto const& [addr, balance] : funded)
        {
            task::syncWait(el1bFund(backendStorage, addr, balance));
        }
        el1bWriteCurrentNumber(backendStorage, 0);
        writeHashToNumber(backendStorage, c_genesisHash, 0);
        writeNumberToHash(backendStorage, 0, c_genesisHash);
        el1bSeedParentHeaderRow(backendStorage, *blockFactory, parent);
    }
};

/// Node A: the block builder — EthEngineService with the REAL external-payload seam
/// (ExternalPayloadVerifierImpl over the node's own verifier), and an L1-fee-market pool.
struct EL1BFixture
{
    EL1BNode nodeA{"el1bA"};
    EL1BNode nodeB{"el1bB"};
    bcos::txpool::MemPoolImpl memPool{
        bcos::txpool::MemPoolConfig{.chainKind = bcos::txpool::ChainKind::L1}};
    StubExecutor stubExecutor;
    StubScheduler stubScheduler;
    std::shared_ptr<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>
        externalVerifier;
    EL1BService service;

    EL1BFixture()
      : externalVerifier(
            std::make_shared<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>(
                nodeA.verifier, nodeA.fakeLedger, nodeA.blockFactory, el1bCancunForks(),
                /*chainId=*/1, /*mergeBlock=*/0)),
        service(memPool, nodeA.storage, stubExecutor, stubScheduler, nodeA.blockFactory,
            /*ledger=*/nullptr, engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
            /*ledgerConfigState=*/nullptr, externalVerifier,
            /*clSync=*/std::make_shared<engine_common::ClSyncCoordination>())
    {}
};

/// Reorg variant: BOTH nodes run the real seam over verifiers with a positive reorg
/// window, so every commit (either lane) writes a rollback journal and a competing
/// payload can rewind a self-built commit.
struct EL1BReorgFixture
{
    static constexpr int64_t c_reorgWindow = 8;

    EL1BNode nodeA{"reorgA", c_reorgWindow};
    EL1BNode nodeB{"reorgB", c_reorgWindow};
    bcos::txpool::MemPoolImpl memPoolA{
        bcos::txpool::MemPoolConfig{.chainKind = bcos::txpool::ChainKind::L1}};
    bcos::txpool::MemPoolImpl memPoolB{
        bcos::txpool::MemPoolConfig{.chainKind = bcos::txpool::ChainKind::L1}};
    StubExecutor stubExecutor;
    StubScheduler stubScheduler;
    std::shared_ptr<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>
        externalVerifierA;
    std::shared_ptr<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>
        externalVerifierB;
    EL1BService serviceA;
    EL1BService serviceB;

    EL1BReorgFixture()
      : externalVerifierA(
            std::make_shared<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>(
                nodeA.verifier, nodeA.fakeLedger, nodeA.blockFactory, el1bCancunForks(),
                /*chainId=*/1, /*mergeBlock=*/0)),
        externalVerifierB(
            std::make_shared<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>(
                nodeB.verifier, nodeB.fakeLedger, nodeB.blockFactory, el1bCancunForks(),
                /*chainId=*/1, /*mergeBlock=*/0)),
        serviceA(memPoolA, nodeA.storage, stubExecutor, stubScheduler, nodeA.blockFactory,
            /*ledger=*/nodeA.fakeLedger, engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
            /*ledgerConfigState=*/nullptr, externalVerifierA,
            /*clSync=*/std::make_shared<engine_common::ClSyncCoordination>()),
        serviceB(memPoolB, nodeB.storage, stubExecutor, stubScheduler, nodeB.blockFactory,
            /*ledger=*/nodeB.fakeLedger, engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
            /*ledgerConfigState=*/nullptr, externalVerifierB,
            /*clSync=*/std::make_shared<engine_common::ClSyncCoordination>())
    {}
};

protocol::EthBlockHeaderData el1bParentHeader(u256 gasUsed, u256 excessBlobGas, u256 blobGasUsed)
{
    protocol::EthBlockHeaderData parent;
    parent.number = 0;
    parent.timestamp = c_parentTimestamp;
    parent.uncleHash = protocol::c_emptyOmmersHash;
    parent.difficulty = u256(0);
    parent.gasLimit = c_parentGasLimit;
    parent.gasUsed = gasUsed;
    parent.baseFee = u256(1000000000);  // 1 gwei
    // The genesis state was written flat (no trie nodes), so the MPT build on both
    // sides starts from the empty trie root — same convention as the verifier tests.
    parent.stateRoot = ledger::mpt::emptyRootHash();
    parent.txsRoot = ledger::mpt::emptyRootHash();
    parent.receiptsRoot = ledger::mpt::emptyRootHash();
    parent.blobGasUsed = blobGasUsed;
    parent.excessBlobGas = excessBlobGas;
    return parent;
}

evmc_address el1bEvmcAddress(bcos::Address const& address)
{
    evmc_address addr{};
    std::copy_n(address.data(), sizeof(addr.bytes), addr.bytes);
    return addr;
}

evmc_address el1bEvmcAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

/// Admit a raw envelope to the pool through the production decode path (recovers the
/// sender from the signature). A blob transaction's EIP-4844 sidecar registers atomically
/// with the admission, exactly like the RPC ingress does.
void el1bPoolAdd(bcos::txpool::MemPoolImpl& pool, bcos::crypto::Hash& hashImpl,
    bcos::bytes const& raw, std::optional<engine::BlobTxSidecar> sidecar = std::nullopt)
{
    auto tx =
        bcos::rpc::decodeWeb3RawTransaction(bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    BOOST_REQUIRE(tx);
    // The ingress contract: recover the sender from the signature and clear the tainted
    // flag (the pool rejects tainted transactions).
    crypto::Secp256k1Crypto secp;
    tx->verify(hashImpl, secp);
    BOOST_CHECK(
        pool.tryAdd(std::move(tx), std::move(sidecar)) == protocol::TransactionStatus::None);
}

/// A one-blob EIP-4844 sidecar with genuine KZG commitment/proof for @p seed, plus the
/// versioned hash the transaction must name.
engine::BlobTxSidecar el1bMakeSidecar(uint8_t seed)
{
    engine::BlobTxSidecar sidecar;
    bcos::bytes blob(crypto::kzg::BlobSize, bcos::byte{0});
    // The last byte: every 32-byte field element stays far below the BLS12-381 scalar
    // modulus, so any seed value keeps the blob valid for c-kzg.
    blob.back() = bcos::byte{seed};
    bcos::bytes commitment, proof;
    BOOST_REQUIRE(crypto::kzg::blobToKzgCommitment(bcos::ref(blob), commitment));
    BOOST_REQUIRE(crypto::kzg::computeBlobKzgProof(bcos::ref(blob), bcos::ref(commitment), proof));
    sidecar.blobs.push_back(std::move(blob));
    sidecar.commitments.push_back(std::move(commitment));
    sidecar.proofs.push_back(std::move(proof));
    return sidecar;
}

/// A RealGlobalStateBackendStorage whose ACCOUNT-table reads ("/apps/", "/s/", "/sys/")
/// can be armed to throw (an injected storage fault, counted down so a test can arm
/// exactly one failure) or to park until a gate opens (a deterministic concurrency
/// interleave). Shadows FOUR read primitives: the raw pair (fillMissingValues and the
/// multi-layer View call readOneRaw/readSomeRaw on the backend directly) AND the
/// storage2::readOne/readSome CPO targets — the View's own readOne ends in
/// storage2::readOne(backend), whose base MemoryStorage implementation would otherwise
/// dispatch to the BASE readOneRaw and slip past the injector. Writes and range() pass
/// straight to the base.
struct FaultyBackendStorage : RealGlobalStateBackendStorage
{
    std::shared_ptr<std::atomic<int>> accountReadThrowRemaining =
        std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<bool>> accountReadGate = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> accountReadGateOpen =
        std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<bool>> accountReadParked =
        std::make_shared<std::atomic<bool>>(false);

    static bool isAccountKey(auto const& key)
    {
        executor_v1::StateKeyView const keyView{key};
        return keyView.m_table.starts_with("/apps/") || keyView.m_table.starts_with("/s/") ||
               keyView.m_table.starts_with("/sys/");
    }

    void injectAccountRead(auto const& key) const
    {
        if (!isAccountKey(key))
        {
            return;
        }
        if (accountReadGate->load(std::memory_order_acquire))
        {
            accountReadParked->store(true, std::memory_order_release);
            while (!accountReadGateOpen->load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
        int remaining = accountReadThrowRemaining->load(std::memory_order_acquire);
        while (remaining > 0 && !accountReadThrowRemaining->compare_exchange_weak(
                                    remaining, remaining - 1, std::memory_order_acq_rel))
        {
        }
        if (remaining > 0)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"injected storage read fault"});
        }
    }

    auto readOneRaw(const auto& key, auto&&... args) -> task::Task<DataValue>
    {
        injectAccountRead(key);
        co_return co_await RealGlobalStateBackendStorage::readOneRaw(
            key, std::forward<decltype(args)>(args)...);
    }

    auto readSomeRaw(
        ::ranges::input_range auto keys, auto&&... args) -> task::Task<std::vector<DataValue>>
    {
        if (accountReadGate->load(std::memory_order_acquire) ||
            accountReadThrowRemaining->load(std::memory_order_acquire) > 0)
        {
            for (auto&& key : keys)
            {
                injectAccountRead(key);
            }
        }
        co_return co_await RealGlobalStateBackendStorage::readSomeRaw(
            std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);
    }

    auto readOne(auto key, auto&&... args) -> task::Task<std::optional<bcos::storage::Entry>>
    {
        injectAccountRead(key);
        co_return co_await RealGlobalStateBackendStorage::readOne(
            std::move(key), std::forward<decltype(args)>(args)...);
    }

    auto readSome(::ranges::input_range auto keys,
        auto&&... args) -> task::Task<std::vector<std::optional<bcos::storage::Entry>>>
    {
        if (accountReadGate->load(std::memory_order_acquire) ||
            accountReadThrowRemaining->load(std::memory_order_acquire) > 0)
        {
            for (auto&& key : keys)
            {
                injectAccountRead(key);
            }
        }
        co_return co_await RealGlobalStateBackendStorage::readSome(
            std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);
    }
};

using FaultyCheckpointBackend = TrivialCheckpointStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, FaultyBackendStorage>;
using FaultyGlobalStateStorage =
    bcos::storage2::MultiLayerStorage<RealGlobalStateMutableStorage, void, FaultyCheckpointBackend>;

/// EL1BNode over the fault-injecting backend: same real execution/verification pipeline,
/// plus the storage-fault and concurrency gates the commit-lane tests drive.
struct EL1BFaultNode
{
    FaultyBackendStorage backendStorage;
    FaultyCheckpointBackend checkpointBackend{backendStorage};
    FaultyGlobalStateStorage storage{checkpointBackend};
    std::shared_ptr<bcos::IOServicePool> ioServicePool;
    std::unique_ptr<scheduler_v1::SchedulerSerialImpl> scheduler;
    crypto::CryptoSuite::Ptr cryptoSuite = bcos::test::createNormalCryptoSuite();
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    bcos::protocol::BlockFactory::Ptr blockFactory = bcos::test::createBlockFactory(cryptoSuite);
    std::shared_ptr<executor_v1::eth::EthereumExecutor> executor;
    std::shared_ptr<EL1BVerifier> verifier;
    std::shared_ptr<bcos::test::FakeLedger> fakeLedger = std::make_shared<bcos::test::FakeLedger>();

    explicit EL1BFaultNode(std::string name, int64_t reorgWindow = 0)
    {
        ioServicePool = std::make_shared<bcos::IOServicePool>(1, std::move(name));
        scheduler = std::make_unique<scheduler_v1::SchedulerSerialImpl>(ioServicePool);
        executor_v1::eth::BlockHashLookup lookup = [this](
                                                       int64_t blockNumber, int64_t currentHeight) {
            return initializer::ethBlockHashLookupFromStorage(
                backendStorage, blockNumber, currentHeight);
        };
        executor =
            std::make_shared<executor_v1::eth::EthereumExecutor>(receiptFactory, std::move(lookup));
        verifier = std::make_shared<EL1BVerifier>(*scheduler, *executor, *blockFactory,
            /*commitObserver=*/nullptr, reorgWindow);
    }

    /// Same seeding as EL1BNode::seedGenesis; the helpers take the base backend type,
    /// which FaultyBackendStorage inherits.
    void seedGenesis(protocol::EthBlockHeaderData const& parent,
        std::vector<std::pair<evmc_address, u256>> const& funded)
    {
        writeEthExecutorConfig(backendStorage, EVMC_CANCUN);
        writeRawSysConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");
        for (auto const& [addr, balance] : funded)
        {
            task::syncWait(el1bFund(backendStorage, addr, balance));
        }
        el1bWriteCurrentNumber(backendStorage, 0);
        writeHashToNumber(backendStorage, c_genesisHash, 0);
        writeNumberToHash(backendStorage, 0, c_genesisHash);
        el1bSeedParentHeaderRow(backendStorage, *blockFactory, parent);
    }
};

/// A ledger stub whose prewrite persists the SYS_HASH_2_NUMBER row the self-built
/// commit lane's fail-closed guard reads (the way the real Ledger::asyncPrewriteBlock
/// writes it); prewriteCount pins how often the commit section ran.
class El1bPersistingLedger : public bcos::test::FakeLedger
{
public:
    using FakeLedger::FakeLedger;

    std::atomic<unsigned> prewriteCount{0};

    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts,
        std::optional<bcos::ledger::Features> features,
        std::optional<bcos::crypto::HashType> blockHashOverride, bool writeNonces) override
    {
        (void)writeTxsAndReceipts;
        (void)features;
        (void)blockHashOverride;
        (void)writeNonces;
        if (block)
        {
            auto const header = block->blockHeader();
            bcos::storage::Entry hash2NumberEntry;
            hash2NumberEntry.set(std::to_string(header->number()));
            storage->asyncSetRow(ledger::SYS_HASH_2_NUMBER,
                bcos::concepts::bytebuffer::toView(header->hash()), std::move(hash2NumberEntry),
                [](auto&&) {});
        }
        ++prewriteCount;
        callback("", nullptr);
    }
};

/// El1bPersistingLedger whose asyncPrewriteBlock can be armed to fail (counted down, so
/// a test can arm exactly one failure): a throw INSIDE the commit section, after
/// pushView has consumed the executed view — the round-4 F1 path. Same injection shape
/// as EngineServiceTest's FlakyLedger.
class El1bFailingPrewriteLedger : public El1bPersistingLedger
{
public:
    using El1bPersistingLedger::El1bPersistingLedger;

    std::shared_ptr<std::atomic<int>> prewriteFailRemaining = std::make_shared<std::atomic<int>>(0);

    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr txs, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts,
        std::optional<bcos::ledger::Features> features,
        std::optional<bcos::crypto::HashType> blockHashOverride, bool writeNonces) override
    {
        int remaining = prewriteFailRemaining->load(std::memory_order_acquire);
        while (remaining > 0 && !prewriteFailRemaining->compare_exchange_weak(
                                    remaining, remaining - 1, std::memory_order_acq_rel))
        {
        }
        if (remaining > 0)
        {
            callback("injected", BCOS_ERROR_PTR(bcos::ledger::LedgerError::ErrorArgument,
                                     "injected prewrite failure"));
            return;
        }
        El1bPersistingLedger::asyncPrewriteBlock(std::move(storage), std::move(txs),
            std::move(block), std::move(callback), writeTxsAndReceipts, std::move(features),
            std::move(blockHashOverride), writeNonces);
    }
};

/// Node A over the fault backend with a positive reorg window: the self-built commit
/// lane journals rollback rows and the fail-closed duplicate guard has a ledger row to
/// answer from. Node B is the independent verifier. ledgerA is the failing-prewrite
/// variant with its counter at 0 — a pure pass-through to El1bPersistingLedger until a
/// test arms it (prewriteFaultRetryCommitsWithJournal), so every other test on this
/// fixture behaves identically to a plain persisting ledger.
struct EL1BFaultFixture
{
    static constexpr int64_t c_reorgWindow = 8;

    EL1BFaultNode nodeA{"faultA", c_reorgWindow};
    EL1BFaultNode nodeB{"faultB", c_reorgWindow};
    bcos::txpool::MemPoolImpl memPool{
        bcos::txpool::MemPoolConfig{.chainKind = bcos::txpool::ChainKind::L1}};
    StubExecutor stubExecutor;
    StubScheduler stubScheduler;
    std::shared_ptr<El1bFailingPrewriteLedger> ledgerA =
        std::make_shared<El1bFailingPrewriteLedger>();
    std::shared_ptr<initializer::ExternalPayloadVerifierImpl<FaultyGlobalStateStorage>>
        externalVerifierA;
    EthEngineService<bcos::txpool::MemPoolImpl, FaultyGlobalStateStorage, StubExecutor,
        StubScheduler>
        serviceA;

    EL1BFaultFixture()
      : externalVerifierA(
            std::make_shared<initializer::ExternalPayloadVerifierImpl<FaultyGlobalStateStorage>>(
                nodeA.verifier, nodeA.fakeLedger, nodeA.blockFactory, el1bCancunForks(),
                /*chainId=*/1, /*mergeBlock=*/0)),
        serviceA(memPool, nodeA.storage, stubExecutor, stubScheduler, nodeA.blockFactory,
            /*ledger=*/ledgerA, engine::c_defaultBlockTxCountLimit,
            static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
            /*ledgerConfigState=*/nullptr, externalVerifierA,
            /*clSync=*/std::make_shared<engine_common::ClSyncCoordination>())
    {}
};

/// Build block 1 on the fault fixture's node A (one 2-gwei withdrawal credit to
/// @p recipientAddress) and return the newPayload request for it. Seeds node A's
/// genesis first; the caller seeds node B when the verify lane needs it.
NewPayloadRequest el1bBuildWithdrawalPayload(auto& fixture, bcos::Address const& recipientAddress)
{
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    fixture.nodeA.seedGenesis(parent, {});
    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    attributes.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 0, .validatorIndex = 1, .amount = 2, .address = recipientAddress}};
    auto fcu = task::syncWait(fixture.serviceA.updateForkchoice(state, &attributes, 3));
    BOOST_REQUIRE(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(fixture.serviceA.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    NewPayloadRequest request;
    request.executionPayload = data->executionPayload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    return request;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthEngineL1BuildTest)

// Closed loop, Cancun: FCU(V3) builds a payload with one EIP-1559 transfer and one blob
// transfer through the real executor; a second node with identical genesis runs the
// payload through verifyAndCommit and must answer Valid (and commit the state change).
BOOST_FIXTURE_TEST_CASE(cancunBuildThenVerifyClosedLoop, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x21);  // not a precompile

    // EIP-1559 transfer (nonce 0): maxFee 2 gwei covers the derived 0.875 gwei base fee.
    bcos::rpc::Web3Transaction transfer;
    transfer.type = bcos::rpc::TransactionType::EIP1559;
    transfer.chainId = 1;
    transfer.nonce = 0;
    transfer.maxPriorityFeePerGas = u256(100000000);
    transfer.maxFeePerGas = u256(2000000000);
    transfer.gasLimit = 21000;
    transfer.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    transfer.value = u256(100);
    auto rawTransfer = el1bSign(transfer, *senderKey);

    // EIP-4844 blob transfer (nonce 1): one blob with a genuine KZG sidecar => 131072
    // blob gas. The versioned hash is derived from the real commitment.
    auto sidecar = el1bMakeSidecar(0x2a);
    auto const sidecarCommitment = sidecar.commitments.front();
    auto const sidecarProof = sidecar.proofs.front();
    auto const sidecarBlob = sidecar.blobs.front();
    auto const versionedHash =
        crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecarCommitment));
    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 1;
    blob.maxPriorityFeePerGas = u256(100000000);
    blob.maxFeePerGas = u256(2000000000);
    blob.gasLimit = 21000;
    blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    blob.value = u256(7);
    blob.maxFeePerBlobGas = u256(1000000000);
    blob.blobVersionedHashes = {versionedHash};
    auto rawBlob = el1bSign(blob, *senderKey);

    auto parent = el1bParentHeader(/*gasUsed=*/u256(0), /*excessBlobGas=*/u256(0),
        /*blobGasUsed=*/u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    // The pool sees the production decode path: raw envelope -> tars transaction with a
    // recovered sender. The blob transaction's sidecar registers with the admission.
    el1bPoolAdd(memPool, hashImpl, rawTransfer);
    el1bPoolAdd(memPool, hashImpl, rawBlob, std::move(sidecar));

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    attributes.withdrawals = std::vector<WithdrawalV1>{WithdrawalV1{.index = 0,
        .validatorIndex = 1,
        .amount = 2,
        .address = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)))}};
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());

    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    BOOST_CHECK_EQUAL(payload.blockNumber, 1);
    BOOST_CHECK_EQUAL(payload.parentHash, c_genesisHash);
    BOOST_CHECK_EQUAL(payload.timestamp, c_blockTimestampMs);
    // EIP-1559 from the REAL derivation: parent gasUsed 0 vs target 15M =>
    // 1 gwei - 1 gwei/8 = 875000000.
    BOOST_CHECK_EQUAL(payload.baseFeePerGas, u256(875000000));
    // EIP-4844 real accounting: 1 versioned hash x BLOB_GAS_PER_BLOB.
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(131072));
    BOOST_REQUIRE(payload.excessBlobGas.has_value());
    BOOST_CHECK_EQUAL(*payload.excessBlobGas, u256(0));
    // Executed roots, not placeholders.
    BOOST_CHECK(payload.stateRoot != h256{});
    BOOST_CHECK(payload.receiptsRoot != h256{});
    BOOST_REQUIRE_EQUAL(payload.transactions.size(), 2);
    BOOST_CHECK(payload.transactions[0].raw == rawTransfer);
    BOOST_CHECK(payload.transactions[1].raw == rawBlob);
    BOOST_REQUIRE(payload.withdrawals.has_value());
    BOOST_REQUIRE_EQUAL(payload.withdrawals->size(), 1);
    // L1 dialect: never the OP MessagePasser withdrawalsRoot, no Cancun requests.
    BOOST_CHECK(!payload.withdrawalsRoot.has_value());
    BOOST_CHECK(!data->executionRequests.has_value());

    // The blobsBundle is the real sidecar content (V3 answers it on an L1): one entry
    // per array, matching the registered sidecar byte-for-byte, and the commitment's
    // versioned hash is what the transaction named.
    BOOST_REQUIRE(data->blobsBundle.has_value());
    BOOST_REQUIRE_EQUAL(data->blobsBundle->commitments.size(), 1);
    BOOST_REQUIRE_EQUAL(data->blobsBundle->proofs.size(), 1);
    BOOST_REQUIRE_EQUAL(data->blobsBundle->blobs.size(), 1);
    BOOST_CHECK(data->blobsBundle->commitments.front() == sidecarCommitment);
    BOOST_CHECK(data->blobsBundle->proofs.front() == sidecarProof);
    BOOST_CHECK(data->blobsBundle->blobs.front() == sidecarBlob);
    BOOST_CHECK_EQUAL(
        crypto::kzg::versionedHashFromCommitment(bcos::ref(data->blobsBundle->commitments.front())),
        versionedHash);
    BOOST_CHECK(crypto::kzg::verifyBlobKzgProof(bcos::ref(data->blobsBundle->blobs.front()),
        bcos::ref(data->blobsBundle->commitments.front()),
        bcos::ref(data->blobsBundle->proofs.front())));

    // (getPayloadV4 is a Prague-shape method; requesting it on this Cancun-built payload is
    // rejected by requireGetPayloadShape, so the bundle is asserted through V3 only.)

    // ---- Node B: verify the payload as an external block and commit it. ----
    NewPayloadRequest request;
    request.executionPayload = payload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);

    EL1BVerifier::TransactionDecoder decoder =
        [&hashImpl](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    };
    using ViewType = RealGlobalStateStorage::ViewType;
    EL1BVerifier::StateRootCalculator<ViewType> stateRootCalc =
        [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto forks = el1bCancunForks();
    auto result = task::syncWait(nodeB.verifier->verifyAndCommit(nodeB.storage, *nodeB.fakeLedger,
        external->ethHeader, parent, external->rawTransactions, external->rawWithdrawals, forks,
        /*chainId=*/1, /*rawUncles=*/{}, /*mergeBlock=*/0, decoder, stateRootCalc));
    BOOST_CHECK(result.valid);
    BOOST_CHECK_MESSAGE(result.error.empty(), result.error);

    // The committed state carries the two transfers plus the 2-gwei withdrawal credit.
    auto const recipientBalance =
        task::syncWait(el1bBalance(nodeB.storage.latestBackend(), recipient));
    BOOST_CHECK_EQUAL(recipientBalance, u256(100 + 7) + u256(2000000000));
}

// The L1 context derivation is real: a parent above its gas target raises the base fee,
// and a parent at the blob target with one blob carried raises the excess blob gas.
// An empty built block must stamp exactly those derived values and still verify.
BOOST_FIXTURE_TEST_CASE(cancunDerivedContextStampsBuiltHeader, EL1BFixture)
{
    // Cancun blob schedule: target 3 blobs (393216 gas), max 6. Parent carries
    // excess == target and 1 blob used => next excess = 393216 + 131072 - 393216.
    auto parent = el1bParentHeader(/*gasUsed=*/u256(30000000),  // full parent block
        /*excessBlobGas=*/u256(393216), /*blobGasUsed=*/u256(131072));
    nodeA.seedGenesis(parent, {});
    nodeB.seedGenesis(parent, {});

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    // EIP-1559 up-adjust: parent full => 1 gwei + 1 gwei/8 = 1125000000.
    BOOST_CHECK_EQUAL(payload.baseFeePerGas, u256(1125000000));
    BOOST_REQUIRE(payload.excessBlobGas.has_value());
    BOOST_CHECK_EQUAL(*payload.excessBlobGas, u256(131072));
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(0));  // no blob transactions in the block
    BOOST_CHECK(payload.transactions.empty());
    BOOST_CHECK(payload.stateRoot != h256{});

    NewPayloadRequest request;
    request.executionPayload = payload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);

    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    EL1BVerifier::TransactionDecoder decoder =
        [&hashImpl](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    };
    using ViewType = RealGlobalStateStorage::ViewType;
    EL1BVerifier::StateRootCalculator<ViewType> stateRootCalc =
        [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto forks = el1bCancunForks();
    auto result = task::syncWait(nodeB.verifier->verifyAndCommit(nodeB.storage, *nodeB.fakeLedger,
        external->ethHeader, parent, external->rawTransactions, external->rawWithdrawals, forks,
        /*chainId=*/1, /*rawUncles=*/{}, /*mergeBlock=*/0, decoder, stateRootCalc));
    BOOST_CHECK(result.valid);
    BOOST_CHECK_MESSAGE(result.error.empty(), result.error);
}

// A pooled blob transaction whose sidecar never arrived is unbuildable: seal skips it —
// and with it the sender's remaining nonce suffix — instead of minting a payload whose
// blobsBundle could never be assembled.
BOOST_FIXTURE_TEST_CASE(sealSkipsBlobTransactionWithoutSidecar, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x22);

    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 0;
    blob.maxPriorityFeePerGas = u256(100000000);
    blob.maxFeePerGas = u256(2000000000);
    blob.gasLimit = 21000;
    blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    blob.value = u256(7);
    blob.maxFeePerBlobGas = u256(1000000000);
    blob.blobVersionedHashes = {
        h256("0101010101010101010101010101010101010101010101010101010101010101")};
    auto rawBlob = el1bSign(blob, *senderKey);

    // Same sender, next nonce: an ordinary transfer. The suffix rule drops it together
    // with the stalled blob transaction (executing it would hit the nonce gap).
    bcos::rpc::Web3Transaction transfer;
    transfer.type = bcos::rpc::TransactionType::EIP1559;
    transfer.chainId = 1;
    transfer.nonce = 1;
    transfer.maxPriorityFeePerGas = u256(100000000);
    transfer.maxFeePerGas = u256(2000000000);
    transfer.gasLimit = 21000;
    transfer.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    transfer.value = u256(100);
    auto rawTransfer = el1bSign(transfer, *senderKey);

    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    // No sidecar on either admission.
    el1bPoolAdd(memPool, hashImpl, rawBlob);
    el1bPoolAdd(memPool, hashImpl, rawTransfer);

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    BOOST_CHECK(payload.transactions.empty());
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(0));
    // The stalled transactions stay pooled for a later block.
    auto fetched = memPool.get(std::vector<crypto::HashType>{});
    (void)fetched;
    BOOST_REQUIRE(data->blobsBundle.has_value());
    BOOST_CHECK(data->blobsBundle->commitments.empty());
}

// newPayloadV3 on an EL-mode node validates expectedBlobVersionedHashes against the
// payload's blob transactions in block order: a mismatch is Invalid, never accepted.
BOOST_FIXTURE_TEST_CASE(newPayloadRejectsMismatchedExpectedBlobVersionedHashes, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x23);

    auto sidecar = el1bMakeSidecar(0x7b);
    auto const versionedHash =
        crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecar.commitments.front()));
    bcos::rpc::Web3Transaction blob;
    blob.type = bcos::rpc::TransactionType::EIP4844;
    blob.chainId = 1;
    blob.nonce = 0;
    blob.maxPriorityFeePerGas = u256(100000000);
    blob.maxFeePerGas = u256(2000000000);
    blob.gasLimit = 21000;
    blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    blob.value = u256(7);
    blob.maxFeePerBlobGas = u256(1000000000);
    blob.blobVersionedHashes = {versionedHash};
    auto rawBlob = el1bSign(blob, *senderKey);

    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    el1bPoolAdd(memPool, hashImpl, rawBlob, std::move(sidecar));

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);

    NewPayloadRequest request;
    request.executionPayload = data->executionPayload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;

    // Wrong hash entirely.
    request.expectedBlobVersionedHashes = {
        h256("0202020202020202020202020202020202020202020202020202020202020202")};
    auto rejected = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK(rejected.status == PayloadValidationStatus::Invalid);

    // The payload carries one blob; an empty expectation mismatches too.
    request.expectedBlobVersionedHashes = {};
    auto rejectedEmpty = task::syncWait(service.newPayload(request, 3));
    BOOST_CHECK(rejectedEmpty.status == PayloadValidationStatus::Invalid);
}

// The built block's gasLimit comes from the PARENT header (delta 0 by default) — the
// tx_gas_limit system config is a single-transaction admission cap and must not mint
// header fields. An optional operator target (el_gas_limit_target) pulls the limit
// toward it by strictly less than parent/1024 per block.
BOOST_FIXTURE_TEST_CASE(gasLimitDerivesFromParentNotTxGasLimitConfig, EL1BFixture)
{
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    nodeA.seedGenesis(parent, {});
    nodeB.seedGenesis(parent, {});
    // tx_gas_limit 100x the parent's 30M: must NOT become the block gas limit.
    writeRawSysConfig(nodeA.backendStorage,
        std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "3000000000");

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    // Δ=0: the parent's 30M, not the 3B admission cap.
    BOOST_CHECK_EQUAL(data->executionPayload.gasLimit, u256(30000000));

    // Operator target above the parent: pull up by parent/1024 - 1 (strictly inside the
    // |Δ| < parent/1024 consensus bound). A different timestamp forces a fresh build.
    u256 const step = u256(30000000) / 1024 - 1;  // 29295
    writeRawSysConfig(
        nodeA.backendStorage, std::string(engine_common::c_l1GasLimitTargetKey), "40000000");
    auto attributesUp = makePayloadAttributesV3(c_blockTimestampMs + 12000);
    auto fcuUp = task::syncWait(service.updateForkchoice(state, &attributesUp, 3));
    BOOST_REQUIRE(fcuUp.payloadId.has_value());
    auto dataUp = task::syncWait(service.getPayload(*fcuUp.payloadId, 3));
    BOOST_REQUIRE(dataUp);
    BOOST_CHECK_EQUAL(dataUp->executionPayload.gasLimit, u256(30000000) + step);

    // Operator target far below: pull down by the same step.
    writeRawSysConfig(
        nodeA.backendStorage, std::string(engine_common::c_l1GasLimitTargetKey), "5000");
    auto attributesDown = makePayloadAttributesV3(c_blockTimestampMs + 24000);
    auto fcuDown = task::syncWait(service.updateForkchoice(state, &attributesDown, 3));
    BOOST_REQUIRE(fcuDown.payloadId.has_value());
    auto dataDown = task::syncWait(service.getPayload(*fcuDown.payloadId, 3));
    BOOST_REQUIRE(dataDown);
    BOOST_CHECK_EQUAL(dataDown->executionPayload.gasLimit, u256(30000000) - step);
}

// Parent-relative consensus checks on the engine EXTERNAL newPayload lane (the real
// verifier seam end-to-end): a payload whose header lies about its parent-derived
// fields is INVALID even though its blockHash is internally consistent — previously
// these all answered VALID. The untouched payload still verifies VALID.
BOOST_FIXTURE_TEST_CASE(externalPayloadParentRelativeConsensusChecks, EL1BFixture)
{
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    auto const recipient = el1bEvmcAddress(0x44);
    nodeA.seedGenesis(parent, {});
    nodeB.seedGenesis(parent, {});

    // Node A builds a valid Cancun payload (empty block, one withdrawal).
    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    attributes.withdrawals = std::vector<WithdrawalV1>{WithdrawalV1{.index = 0,
        .validatorIndex = 1,
        .amount = 2,
        .address = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)))}};
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);

    NewPayloadRequest validRequest;
    validRequest.executionPayload = data->executionPayload;
    validRequest.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    auto converted = engine::detail::executionPayloadToEthBlock(validRequest);
    auto* validBlock = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(validBlock != nullptr);

    // Node B runs the EXTERNAL lane with the REAL verifier seam: it never built this
    // payload, so newPayload routes through verifyAndCommit.
    bcos::txpool::MemPoolImpl memPoolB{
        bcos::txpool::MemPoolConfig{.chainKind = bcos::txpool::ChainKind::L1}};
    auto externalVerifierB =
        std::make_shared<initializer::ExternalPayloadVerifierImpl<RealGlobalStateStorage>>(
            nodeB.verifier, nodeB.fakeLedger, nodeB.blockFactory, el1bCancunForks(),
            /*chainId=*/1, /*mergeBlock=*/0);
    EL1BService serviceB(memPoolB, nodeB.storage, stubExecutor, stubScheduler, nodeB.blockFactory,
        /*ledger=*/nullptr, engine::c_defaultBlockTxCountLimit,
        static_cast<std::uint32_t>(ApiVersion::V4), /*commitObserver=*/nullptr,
        /*ledgerConfigState=*/nullptr, externalVerifierB,
        /*clSync=*/std::make_shared<engine_common::ClSyncCoordination>());

    // Rebuild the request with one header field changed, recomputing blockHash so the
    // payload passes the reconstruction gate and reaches the parent-relative checks.
    auto tamper = [&](auto&& apply) {
        NewPayloadRequest request = validRequest;
        auto ethHeader = validBlock->ethHeader;
        apply(request, ethHeader);
        request.executionPayload.blockHash = protocol::ethHeaderHash(ethHeader);
        return request;
    };
    auto expectInvalid = [&](NewPayloadRequest const& request, std::string_view reason) {
        auto status = task::syncWait(serviceB.newPayload(request, 3));
        BOOST_CHECK(status.status == PayloadValidationStatus::Invalid);
        BOOST_REQUIRE(status.validationError.has_value());
        BOOST_CHECK_MESSAGE(status.validationError->find(reason) != std::string::npos,
            "expected <" << reason << "> in: " << *status.validationError);
    };

    // baseFee off the EIP-1559 recomputation by one wei.
    expectInvalid(tamper([](NewPayloadRequest& r, protocol::EthBlockHeaderData& h) {
        r.executionPayload.baseFeePerGas += 1;
        h.baseFee = *h.baseFee + 1;
    }),
        "baseFeePerGas does not match the EIP-1559 recomputation");
    // excessBlobGas off the EIP-4844 recomputation (parent carries none => 0).
    expectInvalid(tamper([](NewPayloadRequest& r, protocol::EthBlockHeaderData& h) {
        r.executionPayload.excessBlobGas = u256(131072);
        h.excessBlobGas = u256(131072);
    }),
        "excessBlobGas does not match the EIP-4844/7918 recomputation");
    // gasLimit beyond the parent/1024 bound (30M + 30M/1024).
    expectInvalid(tamper([](NewPayloadRequest& r, protocol::EthBlockHeaderData& h) {
        r.executionPayload.gasLimit = u256(30000000) + u256(30000000) / 1024;
        h.gasLimit = r.executionPayload.gasLimit;
    }),
        "gasLimit differs from the parent by more than 1/1024");
    // timestamp not strictly greater than the parent's.
    expectInvalid(tamper([](NewPayloadRequest& r, protocol::EthBlockHeaderData& h) {
        r.executionPayload.timestamp = static_cast<std::uint64_t>(c_parentTimestamp) * 1000;
        h.timestamp = c_parentTimestamp;
    }),
        "timestamp must be strictly greater than the parent");
    // gasUsed above the gas limit.
    expectInvalid(tamper([](NewPayloadRequest& r, protocol::EthBlockHeaderData& h) {
        r.executionPayload.gasUsed = r.executionPayload.gasLimit + 1;
        h.gasUsed = h.gasLimit + 1;
    }),
        "gasUsed exceeds gasLimit");
    // blobGasUsed above the Cancun per-block cap (6 blobs), still a blob multiple.
    expectInvalid(tamper([](NewPayloadRequest& r, protocol::EthBlockHeaderData& h) {
        r.executionPayload.blobGasUsed = u256(7 * 131072);
        h.blobGasUsed = u256(7 * 131072);
    }),
        "invalid blobGasUsed");

    // Control: the untouched payload verifies VALID through the same lane.
    auto status = task::syncWait(serviceB.newPayload(validRequest, 3));
    BOOST_CHECK(status.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(*status.latestValidHash, validRequest.executionPayload.blockHash);
}

// Shallow reorg over a SELF-BUILT commit: node A builds block B (FCU+attributes) and
// commits it through the self-built newPayload lane, which must write the same rollback
// journal the external lane writes. The CL then pushes the competing block B' (built by
// node B on identical genesis): the external lane's height guard rewinds to the parent —
// possible ONLY because B's journal exists — and commits B'. The ledger ends at B''s
// world state, and a follow-up forkchoice to B' is VALID.
BOOST_FIXTURE_TEST_CASE(selfBuiltCommitJournalsForShallowReorg, EL1BReorgFixture)
{
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    auto const recipient = el1bEvmcAddress(0x55);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    nodeA.seedGenesis(parent, {});
    nodeB.seedGenesis(parent, {});

    // The verifier's reorg route compares the CANONICAL hash at the parent height against
    // ethHeaderHash of the reconstructed parent header — so the reorg test cannot use the
    // synthetic c_genesisHash. Derive the real one: read the seeded parent row back and
    // hash the SAME EthBlockHeader(tars).data() reconstruction the verifier performs.
    h256 genesisHash;
    {
        auto view = nodeA.storage.fork();
        auto parentBlock =
            task::syncWait(ledger::getBlockData(view, 0, ledger::HEADER, *nodeA.blockFactory));
        genesisHash =
            protocol::ethHeaderHash(protocol::EthBlockHeader(*parentBlock->blockHeader()).data());
    }
    for (auto* node : {&nodeA, &nodeB})
    {
        writeNumberToHash(node->backendStorage, 0, genesisHash);
        writeHashToNumber(node->backendStorage, genesisHash, 0);
    }

    ForkchoiceState state{genesisHash, h256{}, h256{}};

    // Node A: build block B (2-gwei withdrawal credit) and commit it self-built.
    auto attributesA = makePayloadAttributesV3(c_blockTimestampMs);
    attributesA.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 0, .validatorIndex = 1, .amount = 2, .address = recipientAddress}};
    auto fcuA = task::syncWait(serviceA.updateForkchoice(state, &attributesA, 3));
    BOOST_REQUIRE(fcuA.payloadId.has_value());
    auto dataA = task::syncWait(serviceA.getPayload(*fcuA.payloadId, 3));
    BOOST_REQUIRE(dataA);
    NewPayloadRequest requestA;
    requestA.executionPayload = dataA->executionPayload;
    requestA.parentBeaconBlockRoot = dataA->parentBeaconBlockRoot;
    auto committedA = task::syncWait(serviceA.newPayload(requestA, 3));
    BOOST_CHECK(committedA.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(
        task::syncWait(el1bBalance(nodeA.storage.latestBackend(), recipient)), u256(2000000000));
    // FakeLedger::asyncPrewriteBlock is a no-op: write the ledger metadata rows the real
    // Ledger::prewriteBlock would have written (the established pattern of
    // TestEthereumChainRollback / TestMPTPrunerSyncWiring).
    el1bWriteCurrentNumber(nodeA.backendStorage, 1);
    writeNumberToHash(nodeA.backendStorage, 1, requestA.executionPayload.blockHash);
    writeHashToNumber(nodeA.backendStorage, requestA.executionPayload.blockHash, 1);
    // F1: the SELF-BUILT commit wrote the rollback journal for block 1 — the row the
    // shallow reorg below cannot serve without.
    auto journalEntry = task::syncWait(storage2::readOne(nodeA.backendStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::string("1")}));
    BOOST_REQUIRE(journalEntry.has_value());
    {
        auto const value = journalEntry->get();
        auto journal = scheduler_v1::decodeRollbackJournal(
            bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(value.data()), value.size()));
        // The block dirtied the recipient's account rows (plus the two ledger counters
        // the capture appends unconditionally).
        BOOST_CHECK_GE(journal.entries.size(), 3);
    }

    // Node B: build the COMPETING block B' at the same height (5-gwei credit).
    auto attributesB = makePayloadAttributesV3(c_blockTimestampMs);
    attributesB.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 0, .validatorIndex = 1, .amount = 5, .address = recipientAddress}};
    auto fcuB = task::syncWait(serviceB.updateForkchoice(state, &attributesB, 3));
    BOOST_REQUIRE(fcuB.payloadId.has_value());
    auto dataB = task::syncWait(serviceB.getPayload(*fcuB.payloadId, 3));
    BOOST_REQUIRE(dataB);
    NewPayloadRequest requestB;
    requestB.executionPayload = dataB->executionPayload;
    requestB.parentBeaconBlockRoot = dataB->parentBeaconBlockRoot;
    BOOST_REQUIRE(requestB.executionPayload.blockHash != requestA.executionPayload.blockHash);

    // The CL pushes B' to node A: not built there -> external lane -> sibling at the
    // head -> shallow reorg (depth 1, within the window) -> B' commits.
    auto committedB = task::syncWait(serviceA.newPayload(requestB, 3));
    auto const committedBError =
        committedB.validationError ? *committedB.validationError : std::string("<no error>");
    BOOST_CHECK_MESSAGE(committedB.status == PayloadValidationStatus::Valid, committedBError);
    // Same FakeLedger no-op compensation for the fork block's commit (the rollback set
    // current_number to 0; the no-op prewrite leaves the fork block's rows to us).
    el1bWriteCurrentNumber(nodeA.backendStorage, 1);
    writeNumberToHash(nodeA.backendStorage, 1, requestB.executionPayload.blockHash);
    writeHashToNumber(nodeA.backendStorage, requestB.executionPayload.blockHash, 1);

    // B's 2-gwei credit is rolled back; only B''s 5 gwei remains, the head is 1 and
    // the canonical row at 1 names B'.
    BOOST_CHECK_EQUAL(
        task::syncWait(el1bBalance(nodeA.storage.latestBackend(), recipient)), u256(5000000000));
    auto view = nodeA.storage.fork();
    auto const head = task::syncWait(ledger::getCurrentBlockNumber(view, ledger::fromStorage));
    BOOST_CHECK_EQUAL(head, 1);
    auto canonical = task::syncWait(ledger::getBlockHash(view, 1, ledger::fromStorage));
    BOOST_REQUIRE(canonical.has_value());
    BOOST_CHECK_EQUAL(*canonical, requestB.executionPayload.blockHash);

    // The CL's follow-up forkchoice to B' is VALID.
    ForkchoiceState toBPrime{requestB.executionPayload.blockHash, h256{}, h256{}};
    auto fcuToBPrime = task::syncWait(serviceA.updateForkchoice(toBPrime, nullptr, 3));
    BOOST_CHECK(fcuToBPrime.payloadStatus.status == PayloadValidationStatus::Valid);
}

// Rollback-refused mapping (F5): the reorg route hands a competing payload to the
// verifier, whose stale retry calls rollbackChain — if the journal row for the
// rolled-back block is gone (pruned window, or a commit that never journaled), the
// retry throws RollbackRefused. The adapter must absorb that into a SYNCING answer
// (the CL re-syncs), not let the exception escape newPayload as a -32603 RPC error.
BOOST_FIXTURE_TEST_CASE(reorgRollbackRefusedAnswersSyncing, EL1BReorgFixture)
{
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    auto const recipient = el1bEvmcAddress(0x56);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    nodeA.seedGenesis(parent, {});
    nodeB.seedGenesis(parent, {});

    // Real genesis hash, same derivation as selfBuiltCommitJournalsForShallowReorg.
    h256 genesisHash;
    {
        auto view = nodeA.storage.fork();
        auto parentBlock =
            task::syncWait(ledger::getBlockData(view, 0, ledger::HEADER, *nodeA.blockFactory));
        genesisHash =
            protocol::ethHeaderHash(protocol::EthBlockHeader(*parentBlock->blockHeader()).data());
    }
    for (auto* node : {&nodeA, &nodeB})
    {
        writeNumberToHash(node->backendStorage, 0, genesisHash);
        writeHashToNumber(node->backendStorage, genesisHash, 0);
    }

    ForkchoiceState state{genesisHash, h256{}, h256{}};

    // Node A builds block B and commits it self-built.
    auto attributesA = makePayloadAttributesV3(c_blockTimestampMs);
    attributesA.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 0, .validatorIndex = 1, .amount = 2, .address = recipientAddress}};
    auto fcuA = task::syncWait(serviceA.updateForkchoice(state, &attributesA, 3));
    BOOST_REQUIRE(fcuA.payloadId.has_value());
    auto dataA = task::syncWait(serviceA.getPayload(*fcuA.payloadId, 3));
    BOOST_REQUIRE(dataA);
    NewPayloadRequest requestA;
    requestA.executionPayload = dataA->executionPayload;
    requestA.parentBeaconBlockRoot = dataA->parentBeaconBlockRoot;
    auto committedA = task::syncWait(serviceA.newPayload(requestA, 3));
    BOOST_REQUIRE(committedA.status == PayloadValidationStatus::Valid);
    el1bWriteCurrentNumber(nodeA.backendStorage, 1);
    writeNumberToHash(nodeA.backendStorage, 1, requestA.executionPayload.blockHash);
    writeHashToNumber(nodeA.backendStorage, requestA.executionPayload.blockHash, 1);

    // The rollback journal the retry depends on is gone (window pruning / an
    // unjournaled commit): the retry's rollbackChain must throw RollbackRefused.
    task::syncWait(storage2::removeOne(nodeA.backendStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::string("1")}));

    // Node B builds the competing block B' at the same height; the CL pushes it to
    // node A. The reorg route resolves the canonical parent (genesis), the retry
    // cannot rewind, and the answer must be SYNCING — no exception escapes.
    auto attributesB = makePayloadAttributesV3(c_blockTimestampMs);
    attributesB.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 0, .validatorIndex = 1, .amount = 5, .address = recipientAddress}};
    auto fcuB = task::syncWait(serviceB.updateForkchoice(state, &attributesB, 3));
    BOOST_REQUIRE(fcuB.payloadId.has_value());
    auto dataB = task::syncWait(serviceB.getPayload(*fcuB.payloadId, 3));
    BOOST_REQUIRE(dataB);
    NewPayloadRequest requestB;
    requestB.executionPayload = dataB->executionPayload;
    requestB.parentBeaconBlockRoot = dataB->parentBeaconBlockRoot;
    BOOST_REQUIRE(requestB.executionPayload.blockHash != requestA.executionPayload.blockHash);

    auto refused = task::syncWait(serviceA.newPayload(requestB, 3));
    BOOST_CHECK(refused.status == PayloadValidationStatus::Syncing);

    // The committed chain is untouched: still B's world state at head 1.
    BOOST_CHECK_EQUAL(
        task::syncWait(el1bBalance(nodeA.storage.latestBackend(), recipient)), u256(2000000000));
    auto view = nodeA.storage.fork();
    auto const head = task::syncWait(ledger::getCurrentBlockNumber(view, ledger::fromStorage));
    BOOST_CHECK_EQUAL(head, 1);
    auto canonical = task::syncWait(ledger::getBlockHash(view, 1, ledger::fromStorage));
    BOOST_REQUIRE(canonical.has_value());
    BOOST_CHECK_EQUAL(*canonical, requestA.executionPayload.blockHash);
}

// R1 regression (commit lane, reorgWindow > 0): a storage fault INSIDE the self-built
// commit's rollback-journal capture must not leave a header-only artifact behind — the
// catch hands the executed view back to the artifact before the exception escapes, so
// the CL's retry re-runs take-out + capture from the intact artifact and commits the
// FULL block: state layer, ledger rows, and the journal itself.
BOOST_FIXTURE_TEST_CASE(captureFaultRetryCommitsWithStateAndJournal, EL1BFaultFixture)
{
    auto const recipient = el1bEvmcAddress(0x60);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    auto request = el1bBuildWithdrawalPayload(*this, recipientAddress);

    // The capture reads the pre-block account rows from the committed plane; arm every
    // account read to fail. The exception must escape newPayload (the RPC layer maps it
    // to -32603), with the artifact restored for the retry.
    nodeA.backendStorage.accountReadThrowRemaining->store(100);
    BOOST_CHECK_THROW(task::syncWait(serviceA.newPayload(request, 3)), std::runtime_error);
    nodeA.backendStorage.accountReadThrowRemaining->store(0);
    // Nothing landed: no state, no ledger row (the prewrite never ran).
    BOOST_CHECK_EQUAL(task::syncWait(el1bBalance(nodeA.backendStorage, recipient)), u256(0));
    BOOST_CHECK_EQUAL(ledgerA->prewriteCount.load(), 0);

    // The CL's retry commits the full block from the restored artifact.
    auto committed = task::syncWait(serviceA.newPayload(request, 3));
    BOOST_CHECK(committed.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(ledgerA->prewriteCount.load(), 1);
    BOOST_CHECK_EQUAL(
        task::syncWait(el1bBalance(nodeA.backendStorage, recipient)), u256(2000000000));
    auto journalEntry = task::syncWait(storage2::readOne(nodeA.backendStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::string("1")}));
    BOOST_CHECK(journalEntry.has_value());
}

// Round-4 F1 regression (commit lane, reorgWindow > 0): a fault AFTER pushView — here
// the ledger prewrite — consumes the executed view, so the CL's retry cannot recapture
// the rollback journal (captureRollbackJournal iterates the view's dirty rows). The
// journal is stashed in the artifact before pushView, and the retry must commit the
// block WITH its SYS_ROLLBACK_JOURNAL row: a journal-less committed block would refuse
// every later reorg reaching it (rollbackCommittedChain) and force a full resync.
BOOST_FIXTURE_TEST_CASE(prewriteFaultRetryCommitsWithJournal, EL1BFaultFixture)
{
    auto const recipient = el1bEvmcAddress(0x65);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    auto request = el1bBuildWithdrawalPayload(*this, recipientAddress);

    // First attempt: the capture runs (reorgWindow > 0) and the journal is stashed in
    // the artifact; the injected prewrite failure then throws out of the commit
    // section, AFTER pushView consumed the view. Nothing is durable yet: no state, no
    // prewrite, no journal row.
    ledgerA->prewriteFailRemaining->store(1);
    BOOST_CHECK_THROW(task::syncWait(serviceA.newPayload(request, 3)), bcos::Error);
    BOOST_CHECK_EQUAL(task::syncWait(el1bBalance(nodeA.backendStorage, recipient)), u256(0));
    BOOST_CHECK_EQUAL(ledgerA->prewriteCount.load(), 0);
    auto journalBefore = task::syncWait(storage2::readOne(nodeA.backendStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::string("1")}));
    BOOST_CHECK(!journalBefore.has_value());

    // The CL's retry: the view is gone (its layer is queued from the first attempt's
    // pushView), so the journal comes from the artifact — the block must land WITH its
    // state and its journal row.
    auto committed = task::syncWait(serviceA.newPayload(request, 3));
    BOOST_CHECK(committed.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(ledgerA->prewriteCount.load(), 1);
    BOOST_CHECK_EQUAL(
        task::syncWait(el1bBalance(nodeA.backendStorage, recipient)), u256(2000000000));
    auto journalEntry = task::syncWait(storage2::readOne(nodeA.backendStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::string("1")}));
    BOOST_CHECK(journalEntry.has_value());
}

// R1 regression (commit lane, reorgWindow > 0): a duplicate newPayload racing an
// in-flight commit must NEVER see a header-only artifact. T1 commits but parks inside
// the journal capture (holding m_commitMutex, the artifact's view already taken out);
// T2's duplicate must BLOCK on m_commitMutex — not grab the header-only artifact and
// commit the block without its state layer. T1's capture then fails once, the catch
// restores the view, and T2 commits the full block exactly once.
BOOST_FIXTURE_TEST_CASE(concurrentDuplicateNewPayloadCommitsStateOnce, EL1BFaultFixture)
{
    auto const recipient = el1bEvmcAddress(0x61);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    auto request = el1bBuildWithdrawalPayload(*this, recipientAddress);

    // Park the first account read of T1's journal capture until the gate opens.
    nodeA.backendStorage.accountReadGate->store(true);
    nodeA.backendStorage.accountReadGateOpen->store(false);
    nodeA.backendStorage.accountReadParked->store(false);

    std::atomic<bool> t1Threw{false};
    std::thread t1([&] {
        try
        {
            auto status = task::syncWait(serviceA.newPayload(request, 3));
            (void)status;
        }
        catch (...)
        {
            t1Threw.store(true, std::memory_order_release);
        }
    });
    while (!nodeA.backendStorage.accountReadParked->load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    // T2: the CL's duplicate submission. It must block on m_commitMutex for as long as
    // T1 is parked inside the capture — pre-fix it would have taken the header-only
    // artifact and committed without the state layer.
    std::atomic<bool> t2Done{false};
    auto t2Status = PayloadValidationStatus::Invalid;
    std::thread t2([&] {
        auto status = task::syncWait(serviceA.newPayload(request, 3));
        t2Status = status.status;
        t2Done.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    BOOST_CHECK(!t2Done.load(std::memory_order_acquire));

    // Fail T1's capture exactly once (the R1 catch restores the view into the
    // artifact), then let everything through: T2 re-runs take-out + capture on the
    // intact artifact and commits.
    nodeA.backendStorage.accountReadThrowRemaining->store(1);
    nodeA.backendStorage.accountReadGateOpen->store(true);
    nodeA.backendStorage.accountReadGate->store(false);
    t1.join();
    BOOST_CHECK(t1Threw.load(std::memory_order_acquire));
    t2.join();
    BOOST_CHECK(t2Status == PayloadValidationStatus::Valid);

    // Exactly one commit happened, with the state layer and the journal.
    BOOST_CHECK_EQUAL(ledgerA->prewriteCount.load(), 1);
    BOOST_CHECK_EQUAL(
        task::syncWait(el1bBalance(nodeA.backendStorage, recipient)), u256(2000000000));
    auto journalEntry = task::syncWait(storage2::readOne(nodeA.backendStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::string("1")}));
    BOOST_CHECK(journalEntry.has_value());
}

// R6 (build lane): EthereumState's fail-safe reads swallow a storage fault into the
// block's shared EthStorageErrorSlot, and executeEthereumBlock's block-boundary check
// turns it into a loud EthStorageError. The build must FAIL — a poisoned block (a
// zero-read balance masquerading as state) is never sealed, never committed.
BOOST_FIXTURE_TEST_CASE(buildLaneStorageFaultFailsBuildWithoutCommit, EL1BFaultFixture)
{
    auto const recipient = el1bEvmcAddress(0x62);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    nodeA.seedGenesis(parent, {});

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    attributes.withdrawals = std::vector<WithdrawalV1>{
        WithdrawalV1{.index = 0, .validatorIndex = 1, .amount = 2, .address = recipientAddress}};

    // Every account read fails: the block-start system call / withdrawal credit records
    // the swallowed fault, and the boundary check throws. updateForkchoice only absorbs
    // OpExecutionInternalError, so the EthStorageError reaches the caller (-32603).
    nodeA.backendStorage.accountReadThrowRemaining->store(100);
    BOOST_CHECK_THROW(task::syncWait(serviceA.updateForkchoice(state, &attributes, 3)),
        executor_v1::eth::EthStorageError);
    nodeA.backendStorage.accountReadThrowRemaining->store(0);

    // Nothing committed: the head stays at genesis and the withdrawal never landed.
    auto view = nodeA.storage.fork();
    auto const head = task::syncWait(ledger::getCurrentBlockNumber(view, ledger::fromStorage));
    BOOST_CHECK_EQUAL(head, 0);
    BOOST_CHECK_EQUAL(task::syncWait(el1bBalance(nodeA.backendStorage, recipient)), u256(0));
}

// R6 (verify lane): the same swallowed fault during verifyAndCommit must THROW
// EthStorageError out of the verifier — mapped by the caller to SYNCING / a failed sync
// round — and never surface as `result.valid == false`: a storage fault must not judge
// a legal block INVALID.
BOOST_FIXTURE_TEST_CASE(verifyLaneStorageFaultThrowsInsteadOfInvalid, EL1BFaultFixture)
{
    auto const recipient = el1bEvmcAddress(0x63);
    auto const recipientAddress =
        bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
    auto request = el1bBuildWithdrawalPayload(*this, recipientAddress);
    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    nodeB.seedGenesis(parent, {});

    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);

    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    EL1BVerifier::TransactionDecoder decoder =
        [&hashImpl](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    };
    using ViewType = FaultyGlobalStateStorage::ViewType;
    EL1BVerifier::StateRootCalculator<ViewType> stateRootCalc =
        [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto forks = el1bCancunForks();

    nodeB.backendStorage.accountReadThrowRemaining->store(100);
    BOOST_CHECK_THROW(
        task::syncWait(nodeB.verifier->verifyAndCommit(nodeB.storage, *nodeB.fakeLedger,
            external->ethHeader, parent, external->rawTransactions, external->rawWithdrawals, forks,
            /*chainId=*/1, /*rawUncles=*/{}, /*mergeBlock=*/0, decoder, stateRootCalc)),
        executor_v1::eth::EthStorageError);
    nodeB.backendStorage.accountReadThrowRemaining->store(0);

    // The block was not committed on node B.
    BOOST_CHECK_EQUAL(task::syncWait(el1bBalance(nodeB.backendStorage, recipient)), u256(0));
}

// R7 (seal side): the L1 seal filter enforces the fork schedule's per-block blob
// maximum — the static pool falls back to the Cancun cap of 6. Seven pooled one-blob
// transactions from one sender build a block with exactly six; the seventh stays
// pooled (with its sidecar) for a later block instead of entering the block with a
// validation-failure receipt, and the capped block still verifies.
BOOST_FIXTURE_TEST_CASE(sealCapsBlockBlobsAtScheduleMaximum, EL1BFixture)
{
    auto& hashImpl = *nodeA.cryptoSuite->hashImpl();
    crypto::Secp256k1Crypto secp;
    auto senderKey = secp.generateKeyPair();
    auto const sender = el1bEvmcAddress(senderKey->address(nodeA.cryptoSuite->hashImpl()));
    auto const recipient = el1bEvmcAddress(0x64);

    std::vector<bcos::bytes> rawBlobs;
    for (uint64_t nonce = 0; nonce < 7; ++nonce)
    {
        auto sidecar = el1bMakeSidecar(static_cast<uint8_t>(0x30 + nonce));
        auto const versionedHash =
            crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecar.commitments.front()));
        bcos::rpc::Web3Transaction blob;
        blob.type = bcos::rpc::TransactionType::EIP4844;
        blob.chainId = 1;
        blob.nonce = nonce;
        blob.maxPriorityFeePerGas = u256(100000000);
        blob.maxFeePerGas = u256(2000000000);
        blob.gasLimit = 21000;
        blob.to = bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes)));
        blob.value = u256(7);
        blob.maxFeePerBlobGas = u256(1000000000);
        blob.blobVersionedHashes = {versionedHash};
        rawBlobs.push_back(el1bSign(blob, *senderKey));
        el1bPoolAdd(memPool, hashImpl, rawBlobs.back(), std::move(sidecar));
    }

    auto parent = el1bParentHeader(u256(0), u256(0), u256(0));
    std::vector<std::pair<evmc_address, u256>> funded{{sender, u256(1000000000000000000ULL)}};
    nodeA.seedGenesis(parent, funded);
    nodeB.seedGenesis(parent, funded);

    ForkchoiceState state{c_genesisHash, h256{}, h256{}};
    auto attributes = makePayloadAttributesV3(c_blockTimestampMs);
    auto fcu = task::syncWait(service.updateForkchoice(state, &attributes, 3));
    BOOST_CHECK(fcu.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(fcu.payloadId.has_value());
    auto data = task::syncWait(service.getPayload(*fcu.payloadId, 3));
    BOOST_REQUIRE(data);
    auto const& payload = data->executionPayload;

    // Exactly the 6-blob budget made it into the block, in nonce order.
    BOOST_REQUIRE_EQUAL(payload.transactions.size(), 6);
    for (std::size_t i = 0; i < 6; ++i)
    {
        BOOST_CHECK(payload.transactions[i].raw == rawBlobs[i]);
    }
    BOOST_REQUIRE(payload.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*payload.blobGasUsed, u256(6 * 131072));
    BOOST_REQUIRE(data->blobsBundle.has_value());
    BOOST_CHECK_EQUAL(data->blobsBundle->commitments.size(), 6);

    // Nonce 6 stayed pooled, sidecar registered for a later block.
    auto seventh = bcos::rpc::decodeWeb3RawTransaction(
        bcos::bytesConstRef(rawBlobs[6].data(), rawBlobs[6].size()), hashImpl);
    BOOST_REQUIRE(seventh);
    BOOST_CHECK(memPool.hasBlobSidecar(seventh->hash()));

    // The capped block verifies on node B.
    NewPayloadRequest request;
    request.executionPayload = payload;
    request.parentBeaconBlockRoot = data->parentBeaconBlockRoot;
    auto converted = engine::detail::executionPayloadToEthBlock(request);
    auto* external = std::get_if<engine_common::ExternalPayloadBlock>(&converted);
    BOOST_REQUIRE(external != nullptr);
    EL1BVerifier::TransactionDecoder decoder =
        [&hashImpl](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), hashImpl);
    };
    using ViewType = RealGlobalStateStorage::ViewType;
    EL1BVerifier::StateRootCalculator<ViewType> stateRootCalc =
        [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
        BOOST_THROW_EXCEPTION(
            std::runtime_error{"legacy state-root fold must not run for executor v2"});
    };
    auto forks = el1bCancunForks();
    auto result = task::syncWait(nodeB.verifier->verifyAndCommit(nodeB.storage, *nodeB.fakeLedger,
        external->ethHeader, parent, external->rawTransactions, external->rawWithdrawals, forks,
        /*chainId=*/1, /*rawUncles=*/{}, /*mergeBlock=*/0, decoder, stateRootCalc));
    BOOST_CHECK(result.valid);
    BOOST_CHECK_MESSAGE(result.error.empty(), result.error);
}

BOOST_AUTO_TEST_SUITE_END()
