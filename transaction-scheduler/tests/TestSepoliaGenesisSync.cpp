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
 * @file TestSepoliaGenesisSync.cpp
 * @brief Load the real Sepolia genesis world state (15 pre-funded EOAs, from
 *        eth-clients/sepolia metadata/besu.json) into storage via
 *        ledger::importEthereumGenesisState, prove the resulting op-geth state
 *        root equals the canonical Sepolia genesis root, then verify a real
 *        EMPTY block through EthereumBlockVerifier — the empty block must keep
 *        the state root unchanged (MPT delta empty) and the pre-funded balances
 *        must be readable after commit.
 * @date 2026/8/18
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
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
#include "bcos-ledger/GenesisStateLoader.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/EthTrieRoots.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-utilities/IOServicePool.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/EthereumHost.h"
#include "EthereumBlockHashLookup.h"
#include <bcos-devp2p/sync/Block.h>
#include <bcos-devp2p/sync/HeaderValidator.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <string>
#include <vector>

// Anonymous namespace + ESS prefix: this TU is compiled standalone.
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;

using ESMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using ESBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using ESCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, ESBackendStorage>;
using ESMultiLayerStorage = MultiLayerStorage<ESMutableStorage, void, ESCheckpointBackend>;

/// The real Sepolia genesis allocs (eth-clients/sepolia metadata/besu.json):
/// 15 pre-funded EOAs; the state root over exactly these allocs is the canonical
/// Sepolia genesis state root (verified against the archive RPC).
const char* kSepoliaStateRoot =
    "5eb6e371a698b8d68f665192350ffcecbbbf322916f4b51bd79bb6887da3f494";
const char* kSepoliaGenesisHash =
    "25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9";

struct SepoliaAlloc
{
    std::string_view address;
    std::string_view balanceHex;
};

constexpr SepoliaAlloc kSepoliaAllocs[] = {
    {"a2A6d93439144FFE4D27c9E088dCD8b783946263", "0xD3C21BCECCEDA1000000"},
    {"Bc11295936Aa79d594139de1B2e12629414F3BDB", "0xD3C21BCECCEDA1000000"},
    {"7cF5b79bfe291A67AB02b393E456cCc4c266F753", "0xD3C21BCECCEDA1000000"},
    {"aaec86394441f915bce3e6ab399977e9906f3b69", "0xD3C21BCECCEDA1000000"},
    {"F47CaE1CF79ca6758Bfc787dbD21E6bdBe7112B8", "0xD3C21BCECCEDA1000000"},
    {"d7eDDB78ED295B3C9629240E8924fb8D8874ddD8", "0xD3C21BCECCEDA1000000"},
    {"8b7F0977Bb4f0fBE7076FA22bC24acA043583F5e", "0xD3C21BCECCEDA1000000"},
    {"e2e2659028143784d557bcec6ff3a0721048880a", "0xD3C21BCECCEDA1000000"},
    {"d9a5179f091d85051d3c982785efd1455cec8699", "0xD3C21BCECCEDA1000000"},
    {"beef32ca5b9a198d27B4e02F4c70439fE60356Cf", "0xD3C21BCECCEDA1000000"},
    {"0000006916a87b82333f4245046623b23794c65c", "0x84595161401484A000000"},
    {"b21c33de1fab3fa15499c62b59fe0cc3250020d1", "0x52B7D2DCC80CD2E4000000"},
    {"10F5d45854e038071485AC9e402308cF80D2d2fE", "0x52B7D2DCC80CD2E4000000"},
    {"d7d76c58b3a519e9fA6Cc4D22dC017259BC49F1E", "0x52B7D2DCC80CD2E4000000"},
    {"799D329e5f583419167cD722962485926E338F4a", "0xDE0B6B3A7640000"},
};

evmc_address ESSAddress(std::string_view hexAddr)
{
    evmc_address addr{};
    bcos::bytes raw = bcos::fromHex(std::string(hexAddr));
    std::copy(raw.begin(), raw.end(), addr.bytes);
    return addr;
}

task::Task<void> ESSWriteBlockHash(
    ESBackendStorage& storage, int64_t number, crypto::HashType const& hash)
{
    storage::Entry entry;
    entry.set(hash.asBytes());
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(entry));
}

task::Task<void> ESSWriteCurrentNumber(ESBackendStorage& storage, int64_t number)
{
    storage::Entry entry(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry));
}

task::Task<void> ESSWriteSystemConfig(
    ESBackendStorage& storage, std::string_view key, std::string const& value)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{value, 0}));
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_CONFIG, std::string(key)}, std::move(entry));
}

template <class Storage>
task::Task<u256> ESSReadBalance(Storage& storage, std::string_view hexAddr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, ESSAddress(hexAddr), false);
    co_return co_await acc.balance();
}

class ESSFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    ESBackendStorage backendStorage;
    ESCheckpointBackend checkpointBackend{backendStorage};
    ESMultiLayerStorage multiLayerStorage{checkpointBackend};
    eth::BlockHashLookup blockHashLookup;
    std::shared_ptr<EthereumExecutor> executor;
    bcos::protocol::BlockFactory::Ptr blockFactory;

    ESSFixture()
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

BOOST_AUTO_TEST_SUITE(SepoliaGenesisSyncTest)

// Load the real Sepolia genesis, prove the state root, then verify a real empty
// block: state root unchanged, pre-funded balances readable after commit.
BOOST_FIXTURE_TEST_CASE(loadSepoliaGenesisVerifyEmptyBlock, ESSFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testSepolia");
        SchedulerSerialImpl scheduler(ioServicePool);

        // 1. Load the Sepolia genesis world state (15 pre-funded EOAs).
        std::vector<ledger::Alloc> allocs;
        for (auto const& alloc : kSepoliaAllocs)
        {
            ledger::Alloc a;
            a.address = std::string(alloc.address);
            a.balance = u256(std::string(alloc.balanceHex));
            allocs.push_back(std::move(a));
        }
        ledger::Features features;
        auto genesisStateRoot = co_await ledger::importEthereumGenesisState(
            backendStorage, allocs, *cryptoSuite->hashImpl(), features);

        // 2. The computed root MUST be the canonical Sepolia genesis state root,
        //    and the root node must be persisted as a "/mpt/" row.
        BOOST_CHECK_EQUAL(genesisStateRoot.hex(), std::string(kSepoliaStateRoot));
        auto rootNodeEntry = co_await storage2::readOne(
            backendStorage, storage2::mptNodeStateKey(genesisStateRoot));
        BOOST_CHECK(rootNodeEntry.has_value());

        // 3. Genesis bookkeeping: block-0 hash mapping + height + v2 system config.
        auto genesisHash = crypto::HashType(
            std::string_view(kSepoliaGenesisHash), crypto::HashType::FromHex);
        co_await ESSWriteBlockHash(backendStorage, 0, genesisHash);
        {
            storage::Entry entry;
            entry.set("0");
            co_await storage2::writeOne(backendStorage,
                executor_v1::StateKey{
                    ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(genesisHash)},
                std::move(entry));
        }
        co_await ESSWriteCurrentNumber(backendStorage, 0);
        co_await ESSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await ESSWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        // 4. A real EMPTY block (Sepolia block 1 had no transactions): every root is the
        //    empty trie root and the state root stays the genesis root.
        bcos::protocol::EthBlockHeaderData parentHeader;
        parentHeader.number = 0;
        parentHeader.timestamp = 1633358105;  // Sepolia genesis timestamp
        parentHeader.parentInfo.blockHash = genesisHash;
        parentHeader.difficulty = 0;
        parentHeader.uncleHash = bcos::crypto::HashType{};
        parentHeader.gasLimit = 30000000;
        parentHeader.gasUsed = 0;
        parentHeader.baseFee = u256(1000000000);
        parentHeader.stateRoot = genesisStateRoot;
        parentHeader.txsRoot = ledger::mpt::emptyRootHash();
        parentHeader.receiptsRoot = ledger::mpt::emptyRootHash();

        bcos::protocol::EthBlockHeaderData ethHeader;
        ethHeader.number = 1;
        ethHeader.timestamp = parentHeader.timestamp + 12;
        ethHeader.parentInfo.blockNumber = 0;
        ethHeader.parentInfo.blockHash = genesisHash;
        ethHeader.difficulty = 0;
        ethHeader.uncleHash = bcos::devp2p::sync::emptyOmmersHash();
        ethHeader.gasLimit = 30000000;
        ethHeader.gasUsed = 0;
        ethHeader.baseFee = bcos::devp2p::sync::computeNextBaseFee(parentHeader);
        ethHeader.stateRoot = genesisStateRoot;  // empty block -> state root unchanged
        ethHeader.txsRoot = ledger::mpt::emptyRootHash();
        ethHeader.receiptsRoot = ledger::mpt::emptyRootHash();
        ethHeader.prevRandao = bcos::h256{};
        ethHeader.coinbase = bcos::Address{};
        ethHeader.nonce = bcos::h64{};

        // 5. Verify + commit through the same core the sync path uses.
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);
        auto decoder = [](bcos::bytes const&) -> protocol::Transaction::Ptr {
            return nullptr;  // empty block: no transactions to decode
        };
        using ViewType = ESMultiLayerStorage::ViewType;
        EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::StateRootCalculator<ViewType>
            stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;    // London/Paris/Shanghai active from genesis (explicit 0;
        forks.parisTime = 0;     // unset fields default to UINT64_MAX = never active)
        forks.shanghaiTime = 0;
        forks.cancunTime = std::numeric_limits<uint64_t>::max();
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeader,
            parentHeader, {}, std::nullopt, forks, 11155111, {}, 0, decoder, stateRootCalc);

        BOOST_CHECK(result.valid);
        BOOST_CHECK(result.error.empty());
        BOOST_CHECK_EQUAL(result.stateRoot.hex(), std::string(kSepoliaStateRoot));

        // 6. After commit, a pre-funded account's balance must be readable from the
        //    latest backend (the genesis flat rows + the block's no-op state merge).
        auto balance =
            co_await ESSReadBalance(multiLayerStorage.latestBackend(), kSepoliaAllocs[0].address);
        BOOST_CHECK_EQUAL(balance, u256(std::string(kSepoliaAllocs[0].balanceHex)));
    }());
}

// Verify the real Sepolia BLOCK 1 state root: an empty PoW block whose only state
// change is the 2 ETH coinbase reward. The root must equal the canonical block-1
// root 0xc91d4ecd... — this is exactly what the sync path computes with
// accumulatePoWBlockRewards. Failure here means the reward accounting (or its
// MPT row format) disagrees with geth.
BOOST_FIXTURE_TEST_CASE(sepoliaBlock1PoWRewardStateRoot, ESSFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testSepolia");
        SchedulerSerialImpl scheduler(ioServicePool);

        // 1. Load the Sepolia genesis world state.
        std::vector<ledger::Alloc> allocs;
        for (auto const& alloc : kSepoliaAllocs)
        {
            ledger::Alloc a;
            a.address = std::string(alloc.address);
            a.balance = u256(std::string(alloc.balanceHex));
            allocs.push_back(std::move(a));
        }
        ledger::Features features;
        auto genesisStateRoot = co_await ledger::importEthereumGenesisState(
            backendStorage, allocs, *cryptoSuite->hashImpl(), features);
        BOOST_CHECK_EQUAL(genesisStateRoot.hex(), std::string(kSepoliaStateRoot));

        // 2. PoW block-1 reward: +2 ETH to the block-1 coinbase.
        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        ledgerConfig.setFeatures(features);
        auto view = multiLayerStorage.fork();
        view.newMutable();
        bcos::protocol::EthBlockHeaderData block1;
        block1.number = 1;
        block1.coinbase = bcos::Address(
            std::string_view("0x2f14582947e292a2ecd20c430b46f2d27cfe213c"),
            bcos::Address::FromHex);
        std::vector<bcos::bytes> noUncles;
        co_await accumulatePoWBlockRewards(view, block1, noUncles, ledgerConfig);

        // 3. The resulting state root must be the canonical Sepolia block-1 root.
        auto block1Root = co_await EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            computeMptStateRoot(view, genesisStateRoot, ledgerConfig);
        crypto::HashType expectedBlock1Root(
            bytesConstRef(reinterpret_cast<const bcos::byte*>(
                              "\xc9\x1d\x4e\xcd\x59\xdc\xe3\x06\x7d\x34\x0b\x3a\xad\xfc\x05\x42"
                              "\x97\x4b\x4f\xb4\xdb\x98\xaf\x39\xf9\x80\xa9\x1e\xa0\x0d\xb9\xdc"),
                static_cast<size_t>(32)));
        BOOST_CHECK(block1Root == expectedBlock1Root);
        BOOST_CHECK_EQUAL(block1Root.size(), static_cast<size_t>(32));
    }());
}

BOOST_AUTO_TEST_SUITE_END()
