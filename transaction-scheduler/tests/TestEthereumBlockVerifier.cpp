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
 * @file TestEthereumBlockVerifier.cpp
 * @brief External Ethereum block verification core: locally produce a block
 *        (execute + compute the roots exactly like the engine's buildPayload),
 *        feed it to EthereumBlockVerifier as an "external" block, and check
 *        Valid + atomic commit; then tamper a commitment and check Invalid.
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
#include <boost/test/unit_test.hpp>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <memory>
#include <sstream>

// Anonymous namespace + EEBV prefix: this TU is compiled standalone (it defines the
// same MultiLayerStorage aliases as TestEthereumExecutorScheduler.cpp).
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
using namespace bcos::scheduler_v1;

using EEBVMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using EEBVBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using EEBVCheckpointBackend =
    TrivialCheckpointStorage<StateKey, StateValue, EEBVBackendStorage>;
using EEBVMultiLayerStorage =
    MultiLayerStorage<EEBVMutableStorage, void, EEBVCheckpointBackend>;

static const u256 EEBVFunding = u256(1000000000000000000ULL);  // 1 ETH

evmc_address EEBVAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

/// TransactionImpl subclass exposing markClean() (see TestEthereumExecutorScheduler).
class EEBVTestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

task::Task<void> EEBVFundAccount(EEBVBackendStorage& storage, evmc_address const& addr, u256 balance)
{
    using namespace bcos::ledger::account;
    EVMAccount<EEBVBackendStorage> acc(storage, addr, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce("0");
    co_await acc.setBalance(balance);
}

template <class Storage>
task::Task<u256> EEBVReadBalance(Storage& storage, evmc_address const& addr)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, false);
    co_return co_await acc.balance();
}

evmc_address EEBVAddressFromHex(std::string_view hexAddr)
{
    evmc_address addr{};
    bcos::bytes raw = bcos::fromHex(std::string(hexAddr));
    std::copy(raw.begin(), raw.end(), addr.bytes);
    return addr;
}

evmc::bytes32 EEBVBytes32FromU64(uint64_t value)
{
    evmc::bytes32 out{};
    for (int i = 0; i < 8; ++i)
    {
        out.bytes[31 - i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
    return out;
}

evmc::bytes32 EEBVBytes32FromH256(bcos::h256 const& hash)
{
    evmc::bytes32 out{};
    std::memcpy(out.bytes, hash.data(), sizeof(out.bytes));
    return out;
}

/// Deploy contract code directly into the state (the way the system contracts got
/// their code: ordinary pre-fork deployment transactions — the test shortcuts the
/// deployment tx and writes the code row itself).
task::Task<void> EEBVDeployCode(
    EEBVBackendStorage& storage, evmc_address const& addr, bcos::bytes code)
{
    using namespace bcos::ledger::account;
    EVMAccount<EEBVBackendStorage> acc(storage, addr, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    auto codeHash = bcos::crypto::keccak256Hash(bcos::bytesConstRef(code.data(), code.size()));
    co_await acc.setCode(std::move(code), std::string{}, codeHash);
}

template <class Storage>
task::Task<void> EEBVWriteSlot(
    Storage& storage, evmc_address const& addr, uint64_t slot, evmc::bytes32 const& value)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, false);
    co_await acc.setStorage(EEBVBytes32FromU64(slot), value);
}

template <class Storage>
task::Task<evmc::bytes32> EEBVReadSlot(Storage& storage, evmc_address const& addr, uint64_t slot)
{
    using namespace bcos::ledger::account;
    EVMAccount<std::remove_reference_t<Storage>> acc(storage, addr, false);
    co_return co_await acc.storage(EEBVBytes32FromU64(slot));
}

// EIP-4788 beacon-roots system contract (Cancun): the real deployed runtime code and
// its spec slot layout — timestamp_idx = timestamp % HISTORY_BUFFER_LENGTH holds the
// timestamp, timestamp_idx + HISTORY_BUFFER_LENGTH holds the parent beacon block root.
constexpr std::string_view kEEBVBeaconRootsAddress = "000F3df6D732807Ef1319fB7B8bB8522d0Beac02";
constexpr std::string_view kEEBVBeaconRootsCode =
    "3373fffffffffffffffffffffffffffffffffffffffe14604d57602036146024575f5ffd5b5f358015604957"
    "62001fff810690815414603c575f5ffd5b62001fff01545f5260205ff35b5f5ffd5b62001fff42064281555f35"
    "9062001fff015500";
// EIP-2935 history-storage system contract (Prague): runtime code from the EIP's
// deployment transaction (initcode minus its 9-byte constructor prefix); slot
// (number-1) % HISTORY_SERVE_WINDOW holds the parent block hash.
constexpr std::string_view kEEBVHistoryStorageAddress = "0000F90827F1C53A10CB7A02335B175320002935";
constexpr std::string_view kEEBVHistoryStorageCode =
    "3373fffffffffffffffffffffffffffffffffffffffe14604657602036036042575f35600143038111604257"
    "611fff81430311604257611fff9006545f5260205ff35b5f5ffd5b5f35611fff60014303065500";
// EIP-7002 / EIP-7251 request contracts (Prague): a bare STOP stands in — the call
// succeeds with empty output, so the contracts produce no requests and no state.
constexpr std::string_view kEEBVWithdrawalRequestAddress = "00000961EF480EB55E80D19AD83579A64C007002";
constexpr std::string_view kEEBVConsolidationRequestAddress =
    "0000BBDDC7CE488642FB579F8B00F3A590007251";
constexpr uint64_t kEEBVHistoryBufferLength = 8191;

task::Task<void> EEBVWriteBlockHash(
    EEBVBackendStorage& storage, int64_t number, crypto::HashType const& hash)
{
    storage::Entry entry;
    entry.set(hash.asBytes());
    co_await storage2::writeOne(
        storage, StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(number)}, std::move(entry));
}

task::Task<void> EEBVWriteCurrentNumber(EEBVBackendStorage& storage, int64_t number)
{
    storage::Entry entry(std::to_string(number));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry));
}

task::Task<void> EEBVWriteSystemConfig(EEBVBackendStorage& storage, std::string_view key,
    std::string const& value)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{value, 0}));
    co_await storage2::writeOne(storage,
        StateKey{ledger::SYS_CONFIG, std::string(key)}, std::move(entry));
}

/// A real Web3-shaped EIP-1559 value-transfer tx: EIP-2718 signing payload in
/// extraTransactionBytes + a 65-byte signature, mirroring the eth_sendRawTransaction
/// ingress shape (the same construction engineServiceSealsAndExecutesRealTx uses).
std::shared_ptr<EEBVTestTransactionImpl> EEBVMakeWeb3TransferTx(
    evmc_address const& sender, evmc_address const& recipient, uint64_t value,
    std::string const& nonce)
{
    auto tx = std::make_shared<EEBVTestTransactionImpl>();
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
    inner.data.maxFeePerGas = "0x3b9aca00";  // 1e9 — >= the block base fee (EIP-1559)
    inner.data.maxPriorityFeePerGas = "0x0";
    inner.type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    inner.web3TypedTxKind = 2;  // EIP-1559

    // Signing payload: 0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
    // gasLimit, to, value, data, accessList]).
    bcos::bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));       // chainId
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));       // nonce
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));       // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1000000000));  // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(100000));  // gasLimit
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
    signature[31] = 0x12;  // r != 0
    signature[63] = 0x34;  // s != 0
    signature[64] = 0x01;  // yParity
    inner.signature.assign(signature.begin(), signature.end());

    tx->forceSender(bcos::bytes(std::begin(sender.bytes), std::end(sender.bytes)));
    tx->calculateHash(*bcos::test::createNormalCryptoSuite()->hashImpl());
    tx->markClean();
    return tx;
}

/// XOR fold over the view — the state-root stand-in the engine's buildPayload uses
/// today (real MPT wiring comes with the trie-node persistence in a later step).
template <class View>
task::Task<crypto::HashType> EEBVXorStateRoot(View& view, uint32_t blockVersion,
    std::shared_ptr<bcos::crypto::CryptoSuite> const& cryptoSuite)
{
    auto range = co_await storage2::range(view);
    crypto::HashType totalHash;
    while (auto keyValue = co_await range.next())
    {
        auto& [key, value] = *keyValue;
        executor_v1::StateKeyView viewKey(key);
        auto [tableName, keyName] = viewKey.get();
        storage::Entry entry;
        if (auto* e = std::get_if<storage::Entry>(std::addressof(value)))
        {
            entry = *e;
        }
        else
        {
            entry.setStatus(storage::Entry::DELETED);
        }
        totalHash ^= entry.hash(
            tableName, keyName, *cryptoSuite->hashImpl(), blockVersion);
    }
    co_return totalHash;
}

/// A PoS header skeleton with placeholder roots (the caller fills in the real ones).
/// The uncle hash is the shared canonical empty-ommers hash
/// (bcos::protocol::c_emptyOmmersHash, bcos-rlp-protocol/EthBlockHeader.h).
bcos::protocol::EthBlockHeaderData EEBVPoSHeader(int64_t number, int64_t timestamp,
    bcos::h256 parentHash, uint64_t gasLimit, bcos::u256 baseFee)
{
    bcos::protocol::EthBlockHeaderData header;
    header.number = number;
    header.timestamp = timestamp;
    header.parentInfo.blockNumber = number - 1;
    header.parentInfo.blockHash = parentHash;
    header.difficulty = 0;
    header.uncleHash = bcos::protocol::c_emptyOmmersHash;
    header.gasLimit = gasLimit;
    header.gasUsed = 0;
    header.baseFee = baseFee;
    header.stateRoot = bcos::crypto::HashType(
        std::string_view("0x1111111111111111111111111111111111111111111111111111111111111111"),
        bcos::crypto::HashType::FromHex);
    header.txsRoot = bcos::crypto::HashType(
        std::string_view("0x2222222222222222222222222222222222222222222222222222222222222222"),
        bcos::crypto::HashType::FromHex);
    header.receiptsRoot = bcos::crypto::HashType(
        std::string_view("0x3333333333333333333333333333333333333333333333333333333333333333"),
        bcos::crypto::HashType::FromHex);
    return header;
}

class EEBVFixture
{
public:
    bcos::crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    EEBVBackendStorage backendStorage;
    EEBVCheckpointBackend checkpointBackend{backendStorage};
    EEBVMultiLayerStorage multiLayerStorage{checkpointBackend};
    eth::BlockHashLookup blockHashLookup;
    std::shared_ptr<EthereumExecutor> executor;
    bcos::protocol::BlockFactory::Ptr blockFactory;

    EEBVFixture()
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

BOOST_AUTO_TEST_SUITE(EthereumBlockVerifierTest)

// Produce a block locally (execute + roots), then verify + commit it as an external
// block; the transfer must land in the backend.
BOOST_FIXTURE_TEST_CASE(verifyAndCommitValidExternalBlock, EEBVFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBVValid");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEBVAddress(7);
        auto recipient = EEBVAddress(0x21);  // 0x21 > 0x0a: not a precompile address

        co_await EEBVFundAccount(backendStorage, sender, EEBVFunding);
        co_await EEBVFundAccount(backendStorage, recipient, 0);

        // Genesis block-0 hash mappings + height, so updateForkchoice-style lookups work.
        auto genesisHash = cryptoSuite->hashImpl()->hash(std::string("genesis"));
        co_await EEBVWriteBlockHash(backendStorage, 0, genesisHash);
        {
            storage::Entry entry;
            entry.set("0");
            co_await storage2::writeOne(backendStorage,
                executor_v1::StateKey{
                    ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(genesisHash)},
                std::move(entry));
        }
        co_await EEBVWriteCurrentNumber(backendStorage, 0);
        // v2 executor + a block gas limit for the transfer (gas 21000) to fit.
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        auto tx = EEBVMakeWeb3TransferTx(sender, recipient, 100, "0");
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData());

        // ---- Production side: build the block locally (the engine's buildPayload path). ----
        const int64_t kTimestamp = 12345;  // seconds
        const uint64_t kGasLimit = 30000000;
        const u256 kBaseFee(1000000000);

        ledger::LedgerConfig prodConfig;
        prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig.setEVMCRevision(EVMC_SHANGHAI);
        prodConfig.setGasLimit({kGasLimit, 1});
        prodConfig.setGasPrice({"0x3b9aca00", 1});  // 1e9
        prodConfig.setDifficulty(0);
        evmc::bytes32 randao{};
        randao.bytes[31] = 0xab;
        prodConfig.setPrevRandao(randao);

        bcostars::protocol::BlockHeaderImpl prodHeader;
        prodHeader.setNumber(1);
        prodHeader.setTimestamp(kTimestamp * 1000L);  // ms
        prodHeader.setVersion(prodConfig.compatibilityVersion());
        prodHeader.setParentInfo({0, genesisHash});
        prodHeader.setCoinbase(bcos::Address{});
        prodHeader.setPrevRandao(bcos::h256{});
        prodHeader.setGasLimit(u256(kGasLimit));
        prodHeader.calculateHash(*cryptoSuite->hashImpl());

        auto view = multiLayerStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{tx};
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
        BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);
        // Plain EIP-1559 value transfer to an EOA: intrinsic 21000.
        BOOST_CHECK_EQUAL(receipts[0]->gasUsed(), u256(21000));

        auto computation =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeEthereumRoots(
                    receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});

        // Parent (block 0) header for the PoS field checks and the MPT parent root.
        auto parentHeader = EEBVPoSHeader(0, kTimestamp - 1, bcos::h256{}, kGasLimit, kBaseFee);
        parentHeader.gasUsed = 0;
        parentHeader.stateRoot = ledger::mpt::emptyRootHash();
        parentHeader.txsRoot = ledger::mpt::emptyRootHash();
        parentHeader.receiptsRoot = ledger::mpt::emptyRootHash();

        // MPT state root from the (empty) genesis trie — world state only.
        auto stateRoot =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeMptStateRoot(view, parentHeader.stateRoot, prodConfig);

        // Assemble the external Ethereum header the peer would have sent us.
        auto ethHeader = EEBVPoSHeader(1, kTimestamp, genesisHash, kGasLimit, kBaseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = computation.txsRoot;
        ethHeader.receiptsRoot = computation.receiptsRoot;
        ethHeader.gasUsed = computation.gasUsed;
        ethHeader.logsBloom = computation.logsBloom;
        ethHeader.prevRandao = bcos::h256{};
        ethHeader.coinbase = bcos::Address{};
        ethHeader.nonce = bcos::h64{};

        // ---- Verification side: the external block goes through EthereumBlockVerifier. ----
        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);

        scheduler_v1::EvmcForkTimestamps forks;
        // Unset fields default to UINT64_MAX (never active) — London/Paris/Shanghai must
        // be pinned to 0 explicitly to mean "active from genesis".
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        forks.cancunTime = std::numeric_limits<uint64_t>::max();   // Cancun not yet
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        auto decoder = [tx](bcos::bytes const&) -> protocol::Transaction::Ptr { return tx; };
        using ViewType = EEBVMultiLayerStorage::ViewType;
        // v2 executor: the verifier computes the MPT state root itself; the injected legacy
        // fold must not run (throwing proves the v2 branch won).
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            StateRootCalculator<ViewType>
                stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeader,
            parentHeader, std::vector<bcos::bytes>{raw}, std::nullopt, forks, 1, {}, 0, decoder,
            stateRootCalc);

        BOOST_CHECK(result.valid);
        BOOST_CHECK(result.error.empty());
        BOOST_REQUIRE(result.header);
        BOOST_CHECK_EQUAL(result.header->number(), 1);

        // The committed state must show the transfer executed and be visible in the
        // backend after pushView + mergeBackStorage.
        auto recipientBalance =
            co_await EEBVReadBalance(multiLayerStorage.latestBackend(), recipient);
        BOOST_CHECK_EQUAL(recipientBalance, u256(100));
        auto senderBalance = co_await EEBVReadBalance(multiLayerStorage.latestBackend(), sender);
        // 1 ETH - 100 value - 21000 gas * 1e9 base fee (EIP-1559).
        BOOST_CHECK_EQUAL(
            senderBalance, EEBVFunding - 100 - u256(21000) * u256(1000000000));
    }());
}

// Tampering a header commitment (transactionsRoot) must be rejected without commit.
BOOST_FIXTURE_TEST_CASE(verifyRejectsTamperedTxsRoot, EEBVFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBVInvalid");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEBVAddress(7);
        auto recipient = EEBVAddress(0x21);  // 0x21 > 0x0a: not a precompile address

        co_await EEBVFundAccount(backendStorage, sender, EEBVFunding);
        co_await EEBVFundAccount(backendStorage, recipient, 0);

        auto genesisHash = cryptoSuite->hashImpl()->hash(std::string("genesis"));
        co_await EEBVWriteBlockHash(backendStorage, 0, genesisHash);
        co_await EEBVWriteCurrentNumber(backendStorage, 0);
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        auto tx = EEBVMakeWeb3TransferTx(sender, recipient, 100, "0");
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData());

        const int64_t kTimestamp = 12345;
        const uint64_t kGasLimit = 30000000;
        const u256 kBaseFee(1000000000);

        ledger::LedgerConfig prodConfig;
        prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig.setEVMCRevision(EVMC_SHANGHAI);
        prodConfig.setGasLimit({kGasLimit, 1});
        prodConfig.setGasPrice({"0x3b9aca00", 1});
        prodConfig.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader;
        prodHeader.setNumber(1);
        prodHeader.setTimestamp(kTimestamp * 1000L);
        prodHeader.setVersion(prodConfig.compatibilityVersion());
        prodHeader.setParentInfo({0, genesisHash});
        prodHeader.setGasLimit(u256(kGasLimit));
        prodHeader.calculateHash(*cryptoSuite->hashImpl());

        auto view = multiLayerStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{tx};
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
        auto computation =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeEthereumRoots(
                    receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});

        auto parentHeader = EEBVPoSHeader(0, kTimestamp - 1, bcos::h256{}, kGasLimit, kBaseFee);
        parentHeader.gasUsed = 0;
        parentHeader.stateRoot = ledger::mpt::emptyRootHash();
        parentHeader.txsRoot = ledger::mpt::emptyRootHash();
        parentHeader.receiptsRoot = ledger::mpt::emptyRootHash();

        auto stateRoot =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeMptStateRoot(view, parentHeader.stateRoot, prodConfig);

        auto ethHeader = EEBVPoSHeader(1, kTimestamp, genesisHash, kGasLimit, kBaseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = computation.txsRoot;
        ethHeader.receiptsRoot = computation.receiptsRoot;
        ethHeader.gasUsed = computation.gasUsed;
        ethHeader.logsBloom = computation.logsBloom;
        ethHeader.prevRandao = bcos::h256{};

        // Tamper the transactions root: a different trie commitment.
        ethHeader.txsRoot = bcos::crypto::HashType(
            std::string_view("0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
            bcos::crypto::HashType::FromHex);

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);

        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;    // London/Paris/Shanghai active from genesis (explicit 0;
        forks.parisTime = 0;     // unset fields default to UINT64_MAX = never active)
        forks.shanghaiTime = 0;
        forks.cancunTime = std::numeric_limits<uint64_t>::max();
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        auto decoder = [tx](bcos::bytes const&) -> protocol::Transaction::Ptr { return tx; };
        using ViewType = EEBVMultiLayerStorage::ViewType;
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            StateRootCalculator<ViewType>
                stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeader,
            parentHeader, std::vector<bcos::bytes>{raw}, std::nullopt, forks, 1, {}, 0, decoder,
            stateRootCalc);

        BOOST_CHECK(!result.valid);
        BOOST_CHECK(result.error.find("transactionsRoot") != std::string::npos);
        // The invalid block must not have been committed.
        auto recipientBalance =
            co_await EEBVReadBalance(multiLayerStorage.latestBackend(), recipient);
        BOOST_CHECK_EQUAL(recipientBalance, u256(0));
    }());
}

// makeExecutionBlockHeader must carry every Ethereum field so the stored Tars
// header round-trips to the SAME RLP/hash on resume (the resume anchor re-encodes
// the stored header). Any field dropped by the Tars bridge makes the resume
// parent-hash check fail. The execution header stores the timestamp in FISCO
// milliseconds; the EthBlockHeader(BlockHeader) ctor converts it back to seconds
// (ms -> s) at the RLP boundary, so the resume read-back does NOT divide by 1000
// itself.
BOOST_AUTO_TEST_CASE(tarsExecutionHeaderRoundTripPreservesRlp)
{
    bcos::protocol::EthBlockHeaderData h;
    h.number = 192;
    h.timestamp = 1633358105 + 192;
    h.parentInfo.blockNumber = 191;
    // Non-trivial 32-byte hashes (all distinct, left-padded ones exercise align).
    auto mkHash = [](bcos::byte fill) {
        bcos::bytes b(32, fill);
        return bcos::h256(bcos::bytesConstRef(b.data(), b.size()));
    };
    h.parentInfo.blockHash = mkHash(0xaa);
    h.uncleHash = mkHash(0xbb);
    h.stateRoot = mkHash(0x11);
    h.txsRoot = mkHash(0x22);
    h.receiptsRoot = mkHash(0x33);
    h.difficulty = 131072;
    h.gasLimit = 30000000;
    h.baseFee = bcos::u256(1000000000);

    bcos::bytes orig;
    bcos::codec::rlp::encode(orig, h);
    auto origHash = bcos::crypto::keccak256Hash(bcos::bytesConstRef(orig.data(), orig.size()));

    auto cryptoSuite = bcos::test::createNormalCryptoSuite();
    auto blockFactory = bcos::test::createBlockFactory(cryptoSuite);
    auto header = scheduler_v1::makeExecutionBlockHeader(h, *blockFactory, 0);

    // Resume read-back: the EthBlockHeader(BlockHeader) ctor already converts the stored
    // millisecond timestamp back to seconds — no manual /= 1000 here (dividing again would
    // double-convert and break the round-trip).
    bcos::protocol::EthBlockHeader rebuilt(*header);
    auto data = rebuilt.data();
    bcos::bytes rebuiltRlp;
    bcos::codec::rlp::encode(rebuiltRlp, data);
    auto rebuiltHash =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(rebuiltRlp.data(), rebuiltRlp.size()));

    BOOST_CHECK_MESSAGE(origHash == rebuiltHash,
        "Tars round-trip hash mismatch: orig=" << origHash.hex()
                                               << " rebuilt=" << rebuiltHash.hex());
    BOOST_CHECK(orig == rebuiltRlp);
}

// Cancun+ block through verifyAndCommit: the EIP-4788 block-start system call must
// write the parent beacon block root into the beacon-roots contract storage, and the
// resulting state root must match a block produced with an INDEPENDENT application
// of the EIP-4788 slot formula (manual slot writes on the production side — not the
// same system-call code under test).
BOOST_FIXTURE_TEST_CASE(cancunBeaconRootsSystemCallVerifies, EEBVFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBVCancun");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEBVAddress(7);
        auto recipient = EEBVAddress(0x21);
        auto beaconRoots = EEBVAddressFromHex(kEEBVBeaconRootsAddress);

        co_await EEBVFundAccount(backendStorage, sender, EEBVFunding);
        co_await EEBVFundAccount(backendStorage, recipient, 0);
        co_await EEBVDeployCode(
            backendStorage, beaconRoots, bcos::fromHex(std::string(kEEBVBeaconRootsCode)));

        auto genesisHash = cryptoSuite->hashImpl()->hash(std::string("genesis"));
        co_await EEBVWriteBlockHash(backendStorage, 0, genesisHash);
        co_await EEBVWriteCurrentNumber(backendStorage, 0);
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        auto tx = EEBVMakeWeb3TransferTx(sender, recipient, 100, "0");
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData());

        const int64_t kTimestamp = 12345;  // seconds (the 4788 slot math uses seconds)
        const uint64_t kGasLimit = 30000000;
        const u256 kBaseFee(1000000000);
        const bcos::h256 kParentBeaconRoot = cryptoSuite->hashImpl()->hash(std::string("beacon"));

        // ---- Production side: execute, then apply the EIP-4788 write MANUALLY. ----
        ledger::LedgerConfig prodConfig;
        prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig.setEVMCRevision(EVMC_CANCUN);
        prodConfig.setGasLimit({kGasLimit, 1});
        prodConfig.setGasPrice({"0x3b9aca00", 1});
        prodConfig.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader;
        prodHeader.setNumber(1);
        prodHeader.setTimestamp(kTimestamp * 1000L);
        prodHeader.setVersion(prodConfig.compatibilityVersion());
        prodHeader.setParentInfo({0, genesisHash});
        prodHeader.setGasLimit(u256(kGasLimit));
        prodHeader.calculateHash(*cryptoSuite->hashImpl());

        auto view = multiLayerStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{tx};
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
        BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);

        // The independent EIP-4788 application: slot ts%8191 <- timestamp,
        // slot ts%8191+8191 <- parent beacon root.
        const uint64_t timestampIdx =
            static_cast<uint64_t>(kTimestamp) % kEEBVHistoryBufferLength;
        co_await EEBVWriteSlot(view, beaconRoots, timestampIdx,
            EEBVBytes32FromU64(static_cast<uint64_t>(kTimestamp)));
        co_await EEBVWriteSlot(view, beaconRoots, timestampIdx + kEEBVHistoryBufferLength,
            EEBVBytes32FromH256(kParentBeaconRoot));

        auto computation =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeEthereumRoots(
                    receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});

        auto parentHeader = EEBVPoSHeader(0, kTimestamp - 1, bcos::h256{}, kGasLimit, kBaseFee);
        parentHeader.gasUsed = 0;
        parentHeader.stateRoot = ledger::mpt::emptyRootHash();
        parentHeader.txsRoot = ledger::mpt::emptyRootHash();
        parentHeader.receiptsRoot = ledger::mpt::emptyRootHash();

        auto stateRoot =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeMptStateRoot(view, parentHeader.stateRoot, prodConfig);

        // The external Cancun header: blob-gas fields + parentBeaconRoot present.
        auto ethHeader = EEBVPoSHeader(1, kTimestamp, genesisHash, kGasLimit, kBaseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = computation.txsRoot;
        ethHeader.receiptsRoot = computation.receiptsRoot;
        ethHeader.gasUsed = computation.gasUsed;
        ethHeader.logsBloom = computation.logsBloom;
        ethHeader.prevRandao = bcos::h256{};
        ethHeader.coinbase = bcos::Address{};
        ethHeader.nonce = bcos::h64{};
        ethHeader.blobGasUsed = u256(0);
        ethHeader.excessBlobGas = u256(0);
        ethHeader.parentBeaconRoot = kParentBeaconRoot;

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);

        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        forks.cancunTime = 0;  // Cancun active from genesis
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        auto decoder = [tx](bcos::bytes const&) -> protocol::Transaction::Ptr { return tx; };
        using ViewType = EEBVMultiLayerStorage::ViewType;
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            StateRootCalculator<ViewType>
                stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeader,
            parentHeader, std::vector<bcos::bytes>{raw}, std::nullopt, forks, 1, {}, 0, decoder,
            stateRootCalc);

        BOOST_CHECK(result.valid);
        BOOST_CHECK_MESSAGE(result.error.empty(), result.error);

        // The committed state must carry the beacon-root write the system call made.
        auto committedTs =
            co_await EEBVReadSlot(multiLayerStorage.latestBackend(), beaconRoots, timestampIdx);
        BOOST_CHECK(committedTs == EEBVBytes32FromU64(static_cast<uint64_t>(kTimestamp)));
        auto committedRoot = co_await EEBVReadSlot(
            multiLayerStorage.latestBackend(), beaconRoots, timestampIdx + kEEBVHistoryBufferLength);
        BOOST_CHECK(committedRoot == EEBVBytes32FromH256(kParentBeaconRoot));
    }());
}

// Robustness: a Cancun block on a chain where the beacon-roots contract code was never
// deployed (the EIP-4788 "no code -> fail silently" case) must still verify — evmone
// skips the call and the state root carries no beacon-root write.
BOOST_FIXTURE_TEST_CASE(cancunBeaconRootsMissingCodeSkipsSilently, EEBVFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBVCancunNoCode");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEBVAddress(7);
        auto recipient = EEBVAddress(0x21);
        auto beaconRoots = EEBVAddressFromHex(kEEBVBeaconRootsAddress);

        co_await EEBVFundAccount(backendStorage, sender, EEBVFunding);
        co_await EEBVFundAccount(backendStorage, recipient, 0);
        // NOTE: no beacon-roots code deployed.

        auto genesisHash = cryptoSuite->hashImpl()->hash(std::string("genesis"));
        co_await EEBVWriteBlockHash(backendStorage, 0, genesisHash);
        co_await EEBVWriteCurrentNumber(backendStorage, 0);
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        auto tx = EEBVMakeWeb3TransferTx(sender, recipient, 100, "0");
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData());

        const int64_t kTimestamp = 12345;
        const uint64_t kGasLimit = 30000000;
        const u256 kBaseFee(1000000000);
        const bcos::h256 kParentBeaconRoot = cryptoSuite->hashImpl()->hash(std::string("beacon"));

        ledger::LedgerConfig prodConfig;
        prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig.setEVMCRevision(EVMC_CANCUN);
        prodConfig.setGasLimit({kGasLimit, 1});
        prodConfig.setGasPrice({"0x3b9aca00", 1});
        prodConfig.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader;
        prodHeader.setNumber(1);
        prodHeader.setTimestamp(kTimestamp * 1000L);
        prodHeader.setVersion(prodConfig.compatibilityVersion());
        prodHeader.setParentInfo({0, genesisHash});
        prodHeader.setGasLimit(u256(kGasLimit));
        prodHeader.calculateHash(*cryptoSuite->hashImpl());

        auto view = multiLayerStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{tx};
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
        BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
        // No manual 4788 write: the contract has no code, so the real block carries none.
        auto computation =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeEthereumRoots(
                    receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});

        auto parentHeader = EEBVPoSHeader(0, kTimestamp - 1, bcos::h256{}, kGasLimit, kBaseFee);
        parentHeader.gasUsed = 0;
        parentHeader.stateRoot = ledger::mpt::emptyRootHash();
        parentHeader.txsRoot = ledger::mpt::emptyRootHash();
        parentHeader.receiptsRoot = ledger::mpt::emptyRootHash();

        auto stateRoot =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeMptStateRoot(view, parentHeader.stateRoot, prodConfig);

        auto ethHeader = EEBVPoSHeader(1, kTimestamp, genesisHash, kGasLimit, kBaseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = computation.txsRoot;
        ethHeader.receiptsRoot = computation.receiptsRoot;
        ethHeader.gasUsed = computation.gasUsed;
        ethHeader.logsBloom = computation.logsBloom;
        ethHeader.blobGasUsed = u256(0);
        ethHeader.excessBlobGas = u256(0);
        ethHeader.parentBeaconRoot = kParentBeaconRoot;

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        forks.cancunTime = 0;
        forks.pragueTime = std::numeric_limits<uint64_t>::max();
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        auto decoder = [tx](bcos::bytes const&) -> protocol::Transaction::Ptr { return tx; };
        using ViewType = EEBVMultiLayerStorage::ViewType;
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            StateRootCalculator<ViewType>
                stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeader,
            parentHeader, std::vector<bcos::bytes>{raw}, std::nullopt, forks, 1, {}, 0, decoder,
            stateRootCalc);

        BOOST_CHECK(result.valid);
        BOOST_CHECK_MESSAGE(result.error.empty(), result.error);
        // The beacon-roots account must NOT have appeared in the committed state.
        auto committedTs = co_await EEBVReadSlot(multiLayerStorage.latestBackend(), beaconRoots,
            static_cast<uint64_t>(kTimestamp) % kEEBVHistoryBufferLength);
        BOOST_CHECK(committedTs == evmc::bytes32{});
    }());
}

// Prague block through verifyAndCommit: block-start runs BOTH EIP-4788 and EIP-2935
// (the parent block hash lands in the history-storage contract), and block-end runs
// EIP-7002/7251 (bare-STOP stand-ins: succeed with empty output, no state). The state
// root is cross-checked against independent manual slot applications.
BOOST_FIXTURE_TEST_CASE(pragueSystemCallsVerify, EEBVFixture)
{
    task::syncWait([&, this]() -> task::Task<void> {
        auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "testEBVPrague");
        SchedulerSerialImpl scheduler(ioServicePool);

        auto sender = EEBVAddress(7);
        auto recipient = EEBVAddress(0x21);
        auto beaconRoots = EEBVAddressFromHex(kEEBVBeaconRootsAddress);
        auto historyStorage = EEBVAddressFromHex(kEEBVHistoryStorageAddress);

        co_await EEBVFundAccount(backendStorage, sender, EEBVFunding);
        co_await EEBVFundAccount(backendStorage, recipient, 0);
        co_await EEBVDeployCode(
            backendStorage, beaconRoots, bcos::fromHex(std::string(kEEBVBeaconRootsCode)));
        co_await EEBVDeployCode(
            backendStorage, historyStorage, bcos::fromHex(std::string(kEEBVHistoryStorageCode)));
        co_await EEBVDeployCode(backendStorage,
            EEBVAddressFromHex(kEEBVWithdrawalRequestAddress), bcos::bytes{0x00});
        co_await EEBVDeployCode(backendStorage,
            EEBVAddressFromHex(kEEBVConsolidationRequestAddress), bcos::bytes{0x00});

        auto genesisHash = cryptoSuite->hashImpl()->hash(std::string("genesis"));
        co_await EEBVWriteBlockHash(backendStorage, 0, genesisHash);
        co_await EEBVWriteCurrentNumber(backendStorage, 0);
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version)),
            std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
        co_await EEBVWriteSystemConfig(backendStorage,
            std::string(magic_enum::enum_name(ledger::SystemConfig::tx_gas_limit)), "30000000");

        auto tx = EEBVMakeWeb3TransferTx(sender, recipient, 100, "0");
        auto raw = bcostars::protocol::reassembleWeb3RawTransaction(
            tx->extraTransactionBytes(), tx->signatureData());

        const int64_t kTimestamp = 12345;
        const uint64_t kGasLimit = 30000000;
        const u256 kBaseFee(1000000000);
        const bcos::h256 kParentBeaconRoot = cryptoSuite->hashImpl()->hash(std::string("beacon"));

        ledger::LedgerConfig prodConfig;
        prodConfig.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
        prodConfig.setEVMCRevision(EVMC_PRAGUE);
        prodConfig.setGasLimit({kGasLimit, 1});
        prodConfig.setGasPrice({"0x3b9aca00", 1});
        prodConfig.setDifficulty(0);

        bcostars::protocol::BlockHeaderImpl prodHeader;
        prodHeader.setNumber(1);
        prodHeader.setTimestamp(kTimestamp * 1000L);
        prodHeader.setVersion(prodConfig.compatibilityVersion());
        prodHeader.setParentInfo({0, genesisHash});
        prodHeader.setGasLimit(u256(kGasLimit));
        prodHeader.calculateHash(*cryptoSuite->hashImpl());

        auto view = multiLayerStorage.fork();
        view.newMutable();
        std::vector<protocol::Transaction::Ptr> txs{tx};
        auto receipts = co_await scheduler.executeBlock(
            view, *executor, prodHeader, txs | ::ranges::views::indirect, prodConfig);
        BOOST_REQUIRE_EQUAL(receipts.size(), 1u);
        BOOST_CHECK_EQUAL(receipts[0]->status(), 0);

        // Independent EIP-4788 + EIP-2935 applications on the production side.
        const uint64_t timestampIdx =
            static_cast<uint64_t>(kTimestamp) % kEEBVHistoryBufferLength;
        co_await EEBVWriteSlot(view, beaconRoots, timestampIdx,
            EEBVBytes32FromU64(static_cast<uint64_t>(kTimestamp)));
        co_await EEBVWriteSlot(view, beaconRoots, timestampIdx + kEEBVHistoryBufferLength,
            EEBVBytes32FromH256(kParentBeaconRoot));
        // EIP-2935: slot (number-1) % 8191 <- parent block hash (block 1 -> slot 0).
        co_await EEBVWriteSlot(view, historyStorage, 0, EEBVBytes32FromH256(genesisHash));

        auto computation =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeEthereumRoots(
                    receipts, txs | ::ranges::views::indirect, std::vector<bcos::bytes>{raw});

        auto parentHeader = EEBVPoSHeader(0, kTimestamp - 1, bcos::h256{}, kGasLimit, kBaseFee);
        parentHeader.gasUsed = 0;
        parentHeader.stateRoot = ledger::mpt::emptyRootHash();
        parentHeader.txsRoot = ledger::mpt::emptyRootHash();
        parentHeader.receiptsRoot = ledger::mpt::emptyRootHash();

        auto stateRoot =
            co_await scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
                computeMptStateRoot(view, parentHeader.stateRoot, prodConfig);

        auto ethHeader = EEBVPoSHeader(1, kTimestamp, genesisHash, kGasLimit, kBaseFee);
        ethHeader.stateRoot = stateRoot;
        ethHeader.txsRoot = computation.txsRoot;
        ethHeader.receiptsRoot = computation.receiptsRoot;
        ethHeader.gasUsed = computation.gasUsed;
        ethHeader.logsBloom = computation.logsBloom;
        ethHeader.prevRandao = bcos::h256{};
        ethHeader.coinbase = bcos::Address{};
        ethHeader.nonce = bcos::h64{};
        ethHeader.blobGasUsed = u256(0);
        ethHeader.excessBlobGas = u256(0);
        ethHeader.parentBeaconRoot = kParentBeaconRoot;

        auto fakeLedger = std::make_shared<bcos::test::FakeLedger>();
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor> verifier(
            scheduler, *executor, *blockFactory);
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = 0;
        forks.parisTime = 0;
        forks.shanghaiTime = 0;
        forks.cancunTime = 0;
        forks.pragueTime = 0;  // Prague active from genesis
        forks.osakaTime = std::numeric_limits<uint64_t>::max();
        auto decoder = [tx](bcos::bytes const&) -> protocol::Transaction::Ptr { return tx; };
        using ViewType = EEBVMultiLayerStorage::ViewType;
        scheduler_v1::EthereumBlockVerifier<SchedulerSerialImpl, EthereumExecutor>::
            StateRootCalculator<ViewType>
                stateRootCalc = [](ViewType&, uint32_t) -> task::Task<crypto::HashType> {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"legacy state-root fold must not run for executor v2"});
        };

        auto result = co_await verifier.verifyAndCommit(multiLayerStorage, *fakeLedger, ethHeader,
            parentHeader, std::vector<bcos::bytes>{raw}, std::nullopt, forks, 1, {}, 0, decoder,
            stateRootCalc);

        BOOST_CHECK(result.valid);
        BOOST_CHECK_MESSAGE(result.error.empty(), result.error);

        auto committedRoot = co_await EEBVReadSlot(
            multiLayerStorage.latestBackend(), beaconRoots, timestampIdx + kEEBVHistoryBufferLength);
        BOOST_CHECK(committedRoot == EEBVBytes32FromH256(kParentBeaconRoot));
        auto committedParentHash =
            co_await EEBVReadSlot(multiLayerStorage.latestBackend(), historyStorage, 0);
        BOOST_CHECK(committedParentHash == EEBVBytes32FromH256(genesisHash));
    }());
}

BOOST_AUTO_TEST_SUITE_END()
