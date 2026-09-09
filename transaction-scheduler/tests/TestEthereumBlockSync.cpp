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
 * @file TestEthereumBlockSync.cpp
 * @brief Milestone-2 wiring: download blocks from a fake devp2p peer
 *        (RlpxClient + BlockExchange, with PoS header validation) and feed each
 *        downloaded block through the EthereumBlockVerifier (execute -> roots ->
 *        commit). Produces the chain locally first so every header commitment is
 *        a real executed value, then replays it over the network.
 * @date 2026/8/18
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/testutils/faker/FakeLedger.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/EthTrieRoots.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/Web3RawTransaction.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/EthereumHost.h"
#include "EthereumBlockHashLookup.h"
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-devp2p/sync/BlockExchange.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <bcos-devp2p/test/SyncPeerServer.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

// Anonymous namespace + EBS prefix: this TU is compiled standalone (it defines the
// same MultiLayerStorage aliases as TestEthereumExecutorScheduler.cpp).
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;

using EBSMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using EBSBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using EBSCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, EBSBackendStorage>;
using EBSMultiLayerStorage = MultiLayerStorage<EBSMutableStorage, void, EBSCheckpointBackend>;

static const u256 EBSFunding = u256(1000000000000000000ULL);  // 1 ETH
static const u256 EBSBaseFee = u256(1000000000);              // 1 gwei

evmc_address EBSAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

class EBSTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

task::Task<void> EBSFundAccount(EBSBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<EBSBackendStorage> acc(storage, addr, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

template <class Storage>
task::Task<u256> EBSReadBalance(Storage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, false);
    co_return co_await acc.balance();
}

task::Task<void> EBSWriteBlockHash(
    EBSBackendStorage& storage, int64_t number, crypto::HashType const& hash)
{
    storage::Entry entry;
    entry.set(hash.asBytes());
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(entry));
}

task::Task<void> EBSWriteCurrentNumber(EBSBackendStorage& storage, int64_t number)
{
    storage::Entry entry(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry));
}

task::Task<void> EBSWriteSystemConfig(
    EBSBackendStorage& storage, std::string_view key, std::string const& value)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{value, 0}));
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_CONFIG, std::string(key)}, std::move(entry));
}

/// Web3-shaped EIP-1559 value-transfer tx (maxFeePerGas must cover the block base fee).
std::shared_ptr<EBSTestTransactionImpl> EBSMakeWeb3TransferTx(
    evmc_address const& sender, evmc_address const& recipient, uint64_t value,
    std::string const& nonce, uint64_t maxFeePerGas)
{
    auto tx = std::make_shared<EBSTestTransactionImpl>();
    auto& inner = tx->mutableInner();
    inner.data.version = 1;
    inner.data.to = bcos::toHexStringWithPrefix(
        bcos::bytes(std::begin(recipient.bytes), std::end(recipient.bytes)));
    inner.data.blockLimit = 1000;
    inner.data.chainID = "0x1";
    inner.data.nonce = nonce;
    inner.data.value = [&] {
        std::ostringstream oss;
        oss << "0x" << std::hex << value;
        return oss.str();
    }();
    inner.data.gasPrice = "0x0";
    inner.data.gasLimit = 100000;
    inner.data.maxFeePerGas = [&] {
        std::ostringstream oss;
        oss << "0x" << std::hex << maxFeePerGas;
        return oss.str();
    }();
    inner.data.maxPriorityFeePerGas = "0x0";
    inner.type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = 2;  // EIP-1559

    bcos::bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));                // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));                // nonce
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));                // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(maxFeePerGas));     // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));           // gasLimit
    bcos::codec::rlp::encode(
        body, bcos::Address(bcos::bytesConstRef(recipient.bytes, sizeof(recipient.bytes))));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(value));  // value
    bcos::codec::rlp::encode(body, bcos::bytes{});                 // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);              // empty accessList
    bcos::bytes payloadBytes;
    payloadBytes.push_back(0x02);
    bcos::codec::rlp::encodeHeader(payloadBytes,
        bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payloadBytes.insert(payloadBytes.end(), body.begin(), body.end());
    inner.extraTransactionBytes.assign(payloadBytes.begin(), payloadBytes.end());

    bcos::bytes signature(65, 0);
    signature[31] = 0x12;
    signature[63] = 0x34;
    signature[64] = 0x01;
    inner.signature.assign(signature.begin(), signature.end());

    tx->forceSender(bcos::bytes(std::begin(sender.bytes), std::end(sender.bytes)));
    tx->calculateHash(*bcos::test::createNormalCryptoSuite()->hashImpl());
    tx->markClean();
    return tx;
}

/// A PoS header skeleton with real per-block roots filled in by the caller.
bcos::protocol::EthBlockHeaderData EBSBaseHeader(int64_t number, int64_t timestamp,
    bcos::h256 parentHash, uint64_t gasLimit, bcos::u256 baseFee)
{
    bcos::protocol::EthBlockHeaderData header;
    header.number = number;
    header.timestamp = timestamp;
    header.parentInfo.blockNumber = number - 1;
    header.parentInfo.blockHash = parentHash;
    header.difficulty = 0;
    header.uncleHash = bcos::devp2p::sync::emptyOmmersHash();
    header.gasLimit = gasLimit;
    header.gasUsed = 0;
    header.baseFee = baseFee;
    header.prevRandao = bcos::h256{};
    header.coinbase = bcos::Address{};
    header.nonce = bcos::h64{};
    header.stateRoot = ledger::mpt::emptyRootHash();
    header.txsRoot = ledger::mpt::emptyRootHash();
    header.receiptsRoot = ledger::mpt::emptyRootHash();
    return header;
}

class EBSFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    EBSBackendStorage backendStorage;
    EBSCheckpointBackend checkpointBackend{backendStorage};
    EBSMultiLayerStorage multiLayerStorage{checkpointBackend};
    eth::BlockHashLookup blockHashLookup;
    std::shared_ptr<EthereumExecutor> executor;
    bcos::protocol::BlockFactory::Ptr blockFactory;

    EBSFixture()
    {
        blockHashLookup = [&backend = backendStorage](
                              int64_t blockNumber, int64_t currentHeight) -> evmc::bytes32 {
            return initializer::ethBlockHashLookupFromStorage(
                backend, blockNumber, currentHeight);
        };
        executor = std::make_shared<EthereumExecutor>(receiptFactory, blockHashLookup);
        blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(EthereumBlockSyncTest)

// End-to-end: produce a 2-block chain locally (real executed roots), serve it from a
// fake devp2p peer, download it with BlockExchange (PoS header validation on), and
// verify + commit each block through EthereumBlockVerifier. The committed state must
// reflect both transfers.
BOOST_FIXTURE_TEST_CASE(downloadVerifyCommitChain, EBSFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBSSync");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EBSAddress(7);
        auto recipient1 = EBSAddress(0x21);
        auto recipient2 = EBSAddress(0x22);

        co_await EBSFundAccount(backendStorage, sender, EBSFunding);
        co_await EBSFundAccount(backendStorage, recipient1, 0);
        co_await EBSFundAccount(backendStorage, recipient2, 0);

        // The genesis (block-0) Ethereum header: its keccak(rlp) is the chain anchor.
        auto genesisHeader = EBSBaseHeader(
            0, 1600000000, bcos::h256{}, 30000000, EBSBaseFee);
        genesisHeader.gasUsed = 0;
        auto genesisHash = bcos::devp2p::sync::headerHash(genesisHeader);
        co_await EBSWriteBlockHash(backendStorage, 0, genesisHash);
        {
            storage::Entry entry;
            entry.set("0");
            co_await storage2::writeOne(backendStorage,
                executor_v1::StateKey{
                    ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(genesisHash)},
                std::move(entry));
        }
        co_await EBSWriteCurrentNumber(backendStorage, 0);
        co_await EBSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EBSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        // ---- Production side: build the 2-block chain with real executed values. ----
        // A SEPARATE MultiLayerStorage over the same backend: its layer stack (block 1
        // pushed for block 2's fork) must not leak into the verification side, which
        // starts from the untouched backend (genesis).
        EBSCheckpointBackend prodCheckpoint{backendStorage};
        EBSMultiLayerStorage prodStorage{prodCheckpoint};

        auto tx1 = EBSMakeWeb3TransferTx(sender, recipient1, 100, "0", 1000000000);
        auto tx2 = EBSMakeWeb3TransferTx(sender, recipient2, 50, "1", 1000000000);
        auto raw1 = bcostars::protocol::reassembleWeb3RawTransaction(
            tx1->extraTransactionBytes(), tx1->signatureData());
        auto raw2 = bcostars::protocol::reassembleWeb3RawTransaction(
            tx2->extraTransactionBytes(), tx2->signatureData());

        const int64_t kTs1 = 1600000001;
        const int64_t kTs2 = 1600000002;
        const uint64_t kGasLimit = 30000000;

        // Block 1 — the base fee recomputed from the (empty) genesis: gasUsed=0 makes it
        // drop below 1 gwei immediately, and PoS header validation checks this exactly.
        auto baseFee1 = bcos::devp2p::sync::computeNextBaseFee(genesisHeader);
        ledger::LedgerConfig prodConfig1;
        prodConfig1.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig1.setEVMCRevision(EVMC_SHANGHAI);
        prodConfig1.setGasLimit({kGasLimit, 1});
        prodConfig1.setGasPrice({scheduler_v1::u256ToHexString(baseFee1), 1});
        prodConfig1.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader1;
        prodHeader1.setNumber(1);
        prodHeader1.setTimestamp(kTs1 * 1000L);
        prodHeader1.setParentInfo({0, genesisHash});
        prodHeader1.setGasLimit(u256(kGasLimit));
        prodHeader1.calculateHash(*cryptoSuite->hashImpl());

        auto view1 = prodStorage.fork();
        view1.newMutable();
        std::vector<protocol::Transaction::Ptr> txs1{tx1};
        auto receipts1 = co_await scheduler.executeBlock(
            view1, *executor, prodHeader1, txs1 | ::ranges::views::indirect, prodConfig1);
        BOOST_REQUIRE_EQUAL(receipts1.size(), 1u);
        BOOST_CHECK_EQUAL(receipts1[0]->status(), 0);
        auto comp1 = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeEthereumRoots(receipts1, txs1 | ::ranges::views::indirect,
                std::vector<bcos::bytes>{raw1});
        // MPT state root from the (empty) genesis trie — world state only.
        auto stateRoot1 = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeMptStateRoot(view1, genesisHeader.stateRoot, prodConfig1);
        prodStorage.pushView(std::move(view1));

        auto ethHeader1 = EBSBaseHeader(1, kTs1, genesisHash, kGasLimit, baseFee1);
        ethHeader1.stateRoot = stateRoot1;
        ethHeader1.txsRoot = comp1.txsRoot;
        ethHeader1.receiptsRoot = comp1.receiptsRoot;
        ethHeader1.gasUsed = comp1.gasUsed;
        ethHeader1.logsBloom = comp1.logsBloom;
        bcos::devp2p::sync::Block block1;
        block1.header = ethHeader1;
        bcos::codec::rlp::encode(block1.headerRlp, ethHeader1);
        block1.hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(block1.headerRlp.data(), block1.headerRlp.size()));
        block1.transactions = {raw1};
        block1.uncles = {};

        // Block 2 — base fee recomputed per EIP-1559 from block 1 (gasUsed 21000 < target).
        auto baseFee2 = bcos::devp2p::sync::computeNextBaseFee(ethHeader1);
        ledger::LedgerConfig prodConfig2;
        prodConfig2.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig2.setEVMCRevision(EVMC_SHANGHAI);
        prodConfig2.setGasLimit({kGasLimit, 2});
        prodConfig2.setGasPrice({scheduler_v1::u256ToHexString(baseFee2), 2});
        prodConfig2.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader2;
        prodHeader2.setNumber(2);
        prodHeader2.setTimestamp(kTs2 * 1000L);
        prodHeader2.setParentInfo({1, block1.hash});
        prodHeader2.setGasLimit(u256(kGasLimit));
        prodHeader2.calculateHash(*cryptoSuite->hashImpl());

        auto view2 = prodStorage.fork();
        view2.newMutable();
        std::vector<protocol::Transaction::Ptr> txs2{tx2};
        auto receipts2 = co_await scheduler.executeBlock(
            view2, *executor, prodHeader2, txs2 | ::ranges::views::indirect, prodConfig2);
        BOOST_REQUIRE_EQUAL(receipts2.size(), 1u);
        BOOST_CHECK_EQUAL(receipts2[0]->status(), 0);
        auto comp2 = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeEthereumRoots(receipts2, txs2 | ::ranges::views::indirect,
                std::vector<bcos::bytes>{raw2});
        // MPT state root from block 1's root; the trie nodes written by block 1's build
        // are reachable through view2 (block 1's layer was pushed above).
        auto stateRoot2 = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeMptStateRoot(view2, ethHeader1.stateRoot, prodConfig2);

        auto ethHeader2 = EBSBaseHeader(2, kTs2, block1.hash, kGasLimit, baseFee2);
        ethHeader2.stateRoot = stateRoot2;
        ethHeader2.txsRoot = comp2.txsRoot;
        ethHeader2.receiptsRoot = comp2.receiptsRoot;
        ethHeader2.gasUsed = comp2.gasUsed;
        ethHeader2.logsBloom = comp2.logsBloom;
        bcos::devp2p::sync::Block block2;
        block2.header = ethHeader2;
        bcos::codec::rlp::encode(block2.headerRlp, ethHeader2);
        block2.hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(block2.headerRlp.data(), block2.headerRlp.size()));
        block2.transactions = {raw2};
        block2.uncles = {};

        // The fake peer serves a full chain indexed by block number: genesis at [0], then
        // the two real blocks.
        bcos::devp2p::sync::Block genesisBlock;
        genesisBlock.header = genesisHeader;
        bcos::codec::rlp::encode(genesisBlock.headerRlp, genesisHeader);
        genesisBlock.hash = genesisHash;
        genesisBlock.transactions = {};
        genesisBlock.uncles = {};
        std::vector<bcos::devp2p::sync::Block> chain{genesisBlock, block1, block2};

        // ---- Network side: serve the chain from a fake peer. ----
        bcos::devp2p::rlpx::EccKeyPair serverKey;
        bcos::devp2p::rlpx::EccKeyPair clientKey;
        bcos::devp2p::rlpx::PeerConfig serverConfig;
        serverConfig.clientId = "fake-peer";
        bcos::devp2p::rlpx::RlpxServer server(serverKey, 0, serverConfig);
        uint16_t port = server.port();

        std::thread serverThread([&] {
            try
            {
                auto established = server.accept();
                bcos::devp2p::test::serveRequests(established.session, chain);
            }
            catch (...)
            {
            }
        });

        bcos::devp2p::rlpx::PeerConfig clientConfig;
        clientConfig.host = "127.0.0.1";
        clientConfig.port = port;
        clientConfig.peerPublicKey = serverKey.publicKey();
        clientConfig.clientId = "sync-verify-test";

        // ---- Download + verify + commit. ----
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);

        std::map<bcos::bytes, protocol::Transaction::Ptr> rawToTx;
        rawToTx[raw1] = tx1;
        rawToTx[raw2] = tx2;
        auto decoder = [&rawToTx](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
            return rawToTx.at(raw);
        };

        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;    // London/Paris/Shanghai active from genesis (explicit 0;
        forks.parisTime = 0;     // unset fields default to UINT64_MAX = never active)
        forks.shanghaiTime = 0;
        forks.cancunTime = std::numeric_limits<uint64_t>::max();
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        using ViewType = EBSMultiLayerStorage::ViewType;
        // v2 (executor_version=2) must NOT reach the injected legacy fold — the verifier
        // computes the MPT state root itself. Throwing here proves the v2 branch won.
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::StateRootCalculator<ViewType>
            stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        {
            bcos::devp2p::rlpx::RlpxClient client(std::move(clientKey), clientConfig);
            auto established = client.connect();

            // The synthetic headers are London-style (no withdrawals/blob/requests
            // fields); disable the post-London forks so the fail-closed field-presence
            // checks match what this chain actually carries.
            bcos::devp2p::sync::ChainConfig devp2pConfig{.chainId = 1};
            devp2pConfig.shanghaiTime = std::numeric_limits<uint64_t>::max();
            devp2pConfig.cancunTime = std::numeric_limits<uint64_t>::max();
            devp2pConfig.pragueTime = std::numeric_limits<uint64_t>::max();
            bcos::devp2p::sync::BlockExchange exchange(1, genesisHeader, devp2pConfig);

            auto prevHeader = genesisHeader;
            std::vector<bcos::devp2p::sync::Block> downloaded;
            try
            {
                exchange.downloadRange(established.session, chain.size() - 1,
                    [&](bcos::devp2p::sync::Block const& block) {
                        auto result = task::syncWait(verifier.verifyAndCommit(multiLayerStorage,
                            *fakeLedger, block.header, prevHeader, block.transactions,
                            block.withdrawals, forks, 1, block.uncles, 0, decoder, stateRootCalc));
                        if (!result.valid)
                        {
                            std::cerr << "[EBS] verifyAndCommit INVALID at block "
                                      << block.number() << ": " << result.error << std::endl;
                            BOOST_TEST_MESSAGE(
                                "verifyAndCommit invalid at block " << block.number() << ": "
                                                                    << result.error
                                                                    << " | receipts="
                                                                    << result.receipts.size()
                                                                    << " status="
                                                                    << (result.receipts.empty() ?
                                                                            -1 :
                                                                            result.receipts[0]->status())
                                                                    << " gasUsed="
                                                                    << (result.receipts.empty() ?
                                                                            -1 :
                                                                            result.receipts[0]->gasUsed()));
                        }
                        BOOST_REQUIRE(result.valid);
                        BOOST_CHECK(result.error.empty());
                        prevHeader = block.header;
                        downloaded.push_back(block);
                    });
            }
            catch (std::exception const& e)
            {
                BOOST_TEST_MESSAGE("downloadRange threw: " << e.what());
                throw;
            }

            BOOST_REQUIRE_EQUAL(downloaded.size(), 2u);
            BOOST_CHECK(downloaded[0].hash == block1.hash);
            BOOST_CHECK(downloaded[1].hash == block2.hash);
        }  // close the client connection

        serverThread.join();

        // ---- Committed state: both transfers landed. ----
        auto balanceR1 =
            co_await EBSReadBalance(multiLayerStorage.latestBackend(), recipient1);
        BOOST_CHECK_EQUAL(balanceR1, u256(100));
        auto balanceR2 =
            co_await EBSReadBalance(multiLayerStorage.latestBackend(), recipient2);
        BOOST_CHECK_EQUAL(balanceR2, u256(50));
        auto balanceSender =
            co_await EBSReadBalance(multiLayerStorage.latestBackend(), sender);
        auto baseFee2u = bcos::devp2p::sync::computeNextBaseFee(ethHeader1);
        BOOST_CHECK_EQUAL(balanceSender, EBSFunding - 100 - u256(21000) * baseFee1 - 50 -
                                             u256(21000) * baseFee2u);
    }());
}

// A peer serving a block whose header commits to a different transactionsRoot than the
// execution produces must be rejected: the download completes (PoS fields intact) but the
// verifier marks the block invalid and nothing is committed.
BOOST_FIXTURE_TEST_CASE(downloadRejectsTamperedCommitment, EBSFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBSBad");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EBSAddress(7);
        auto recipient1 = EBSAddress(0x21);

        co_await EBSFundAccount(backendStorage, sender, EBSFunding);
        co_await EBSFundAccount(backendStorage, recipient1, 0);

        auto genesisHeader = EBSBaseHeader(0, 1600000000, bcos::h256{}, 30000000, EBSBaseFee);
        genesisHeader.gasUsed = 0;
        auto genesisHash = bcos::devp2p::sync::headerHash(genesisHeader);
        co_await EBSWriteBlockHash(backendStorage, 0, genesisHash);
        co_await EBSWriteCurrentNumber(backendStorage, 0);
        co_await EBSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EBSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        // Production side on a SEPARATE MultiLayerStorage over the same backend, so its
        // layer stack cannot leak into the verification side.
        EBSCheckpointBackend prodCheckpoint{backendStorage};
        EBSMultiLayerStorage prodStorage{prodCheckpoint};
        auto view1 = prodStorage.fork();
        view1.newMutable();
        auto tx1 = EBSMakeWeb3TransferTx(sender, recipient1, 100, "0", 1000000000);
        auto raw1 = bcostars::protocol::reassembleWeb3RawTransaction(
            tx1->extraTransactionBytes(), tx1->signatureData());
        const int64_t kTs1 = 1600000001;
        const uint64_t kGasLimit = 30000000;

        // Block 1 — base fee recomputed from the empty genesis (PoS validation checks it).
        auto baseFee1 = bcos::devp2p::sync::computeNextBaseFee(genesisHeader);
        ledger::LedgerConfig prodConfig1;
        prodConfig1.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig1.setEVMCRevision(EVMC_SHANGHAI);
        prodConfig1.setGasLimit({kGasLimit, 1});
        prodConfig1.setGasPrice({scheduler_v1::u256ToHexString(baseFee1), 1});
        prodConfig1.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader1;
        prodHeader1.setNumber(1);
        prodHeader1.setTimestamp(kTs1 * 1000L);
        prodHeader1.setParentInfo({0, genesisHash});
        prodHeader1.setGasLimit(u256(kGasLimit));
        prodHeader1.calculateHash(*cryptoSuite->hashImpl());

        std::vector<protocol::Transaction::Ptr> txs1{tx1};
        auto receipts1 = co_await scheduler.executeBlock(
            view1, *executor, prodHeader1, txs1 | ::ranges::views::indirect, prodConfig1);
        auto comp1 = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeEthereumRoots(receipts1, txs1 | ::ranges::views::indirect,
                std::vector<bcos::bytes>{raw1});
        auto stateRoot1 = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeMptStateRoot(view1, genesisHeader.stateRoot, prodConfig1);

        auto ethHeader1 = EBSBaseHeader(1, kTs1, genesisHash, kGasLimit, baseFee1);
        ethHeader1.stateRoot = stateRoot1;
        ethHeader1.txsRoot = comp1.txsRoot;
        ethHeader1.receiptsRoot = comp1.receiptsRoot;
        ethHeader1.gasUsed = comp1.gasUsed;
        ethHeader1.logsBloom = comp1.logsBloom;
        // Tamper the transactions root the peer claims.
        ethHeader1.txsRoot = bcos::crypto::HashType(
            std::string_view("0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            bcos::crypto::HashType::FromHex);

        bcos::devp2p::sync::Block block1;
        block1.header = ethHeader1;
        bcos::codec::rlp::encode(block1.headerRlp, ethHeader1);
        block1.hash = bcos::crypto::keccak256Hash(
            bcos::bytesConstRef(block1.headerRlp.data(), block1.headerRlp.size()));
        block1.transactions = {raw1};
        block1.uncles = {};
        bcos::devp2p::sync::Block genesisBlock;
        genesisBlock.header = genesisHeader;
        bcos::codec::rlp::encode(genesisBlock.headerRlp, genesisHeader);
        genesisBlock.hash = genesisHash;
        genesisBlock.transactions = {};
        genesisBlock.uncles = {};
        std::vector<bcos::devp2p::sync::Block> chain{genesisBlock, block1};

        bcos::devp2p::rlpx::EccKeyPair serverKey;
        bcos::devp2p::rlpx::EccKeyPair clientKey;
        bcos::devp2p::rlpx::PeerConfig serverConfig;
        serverConfig.clientId = "fake-peer";
        bcos::devp2p::rlpx::RlpxServer server(serverKey, 0, serverConfig);
        uint16_t port = server.port();

        std::thread serverThread([&] {
            try
            {
                auto established = server.accept();
                bcos::devp2p::test::serveRequests(established.session, chain);
            }
            catch (...)
            {
            }
        });

        bcos::devp2p::rlpx::PeerConfig clientConfig;
        clientConfig.host = "127.0.0.1";
        clientConfig.port = port;
        clientConfig.peerPublicKey = serverKey.publicKey();
        clientConfig.clientId = "sync-verify-bad";

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);
        std::map<bcos::bytes, protocol::Transaction::Ptr> rawToTx;
        rawToTx[raw1] = tx1;
        auto decoder = [&rawToTx](bcos::bytes const& raw) -> protocol::Transaction::Ptr {
            return rawToTx.at(raw);
        };
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;    // London/Paris/Shanghai active from genesis (explicit 0;
        forks.parisTime = 0;     // unset fields default to UINT64_MAX = never active)
        forks.shanghaiTime = 0;
        forks.cancunTime = std::numeric_limits<uint64_t>::max();
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        using ViewType = EBSMultiLayerStorage::ViewType;
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::StateRootCalculator<ViewType>
            stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        bool rejected = false;
        {
            bcos::devp2p::rlpx::RlpxClient client(std::move(clientKey), clientConfig);
            auto established = client.connect();
            // Same London-style synthetic headers as above: keep the post-London forks
            // disabled in the devp2p header validator.
            bcos::devp2p::sync::ChainConfig devp2pConfig{.chainId = 1};
            devp2pConfig.shanghaiTime = std::numeric_limits<uint64_t>::max();
            devp2pConfig.cancunTime = std::numeric_limits<uint64_t>::max();
            devp2pConfig.pragueTime = std::numeric_limits<uint64_t>::max();
            bcos::devp2p::sync::BlockExchange exchange(1, genesisHeader, devp2pConfig);
            exchange.downloadRange(established.session, chain.size() - 1,
                [&](bcos::devp2p::sync::Block const& block) {
                    auto result = task::syncWait(verifier.verifyAndCommit(multiLayerStorage,
                        *fakeLedger, block.header, genesisHeader, block.transactions,
                        block.withdrawals, forks, 1, block.uncles, 0, decoder, stateRootCalc));
                    BOOST_CHECK(!result.valid);
                    BOOST_CHECK(result.error.find("transactionsRoot") != std::string::npos);
                    rejected = true;
                });
        }  // close the client connection

        serverThread.join();
        BOOST_CHECK(rejected);
        // Nothing committed.
        auto balanceR1 =
            co_await EBSReadBalance(multiLayerStorage.latestBackend(), recipient1);
        BOOST_CHECK_EQUAL(balanceR1, u256(0));
    }());
}

BOOST_AUTO_TEST_SUITE_END()
