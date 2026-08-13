/// @file TestOpStackExecution.cpp
/// @brief OP Stack (Karst) execution lane: consensus-grade 0x7E decoding,
///        deposit execution through EthereumExecutor::executeDeposit, and the
///        feature_l2_ethereum_compat dispatch of normal transactions onto
///        opTransition (L1 data fee / fee vault routing), with the pure-Ethereum
///        lane as differential control.
///
/// Standalone CHECK-style runner (same pattern as TestEthereumStateSmoke.cpp):
/// NDEBUG-independent, registered with add_test().

#include "ethereum-executor/EthereumExecutor.h"
#include "ethereum-executor/OpStackTransition.h"
#include "ethereum-executor/tests/TestMemoryStorage.h"

#include "bcos-codec/rlp/Common.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/TBBWait.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <evmone/evmone.h>
#include <iostream>
#include <memory>

namespace
{
int g_failures = 0;

void checkImpl(bool ok, const char* expr, const char* file, int line)
{
    if (!ok)
    {
        std::cerr << file << ':' << line << ": CHECK failed: " << expr << '\n';
        ++g_failures;
    }
}
}  // namespace

#define CHECK(expr) checkImpl(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

namespace
{
using namespace bcos;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::eth;
namespace opstack = bcos::evm::opstack;

constexpr uint64_t kBaseFee = 7;

evmc_address makeAddress(uint8_t seed)
{
    evmc_address addr{};
    addr.bytes[19] = seed;
    return addr;
}

task::Task<void> seedAccount(
    MutableStorage& storage, evmc_address const& addr, u256 balance, uint64_t nonce)
{
    ledger::account::EVMAccount<MutableStorage> acc(storage, addr, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    co_await acc.setNonce(std::to_string(nonce));
    co_await acc.setBalance(balance);
}

/// slot 3 of L1Block.sol: base_fee_scalar packed into bytes [16,20) (BE u32);
/// blob_base_fee_scalar ([20,24)) left 0.
evmc::bytes32 packSlot3(uint32_t baseFeeScalar)
{
    evmc::bytes32 slot3{};
    for (int i = 0; i < 4; ++i)
    {
        slot3.bytes[16 + i] = static_cast<uint8_t>(baseFeeScalar >> (8 * (3 - i)));
    }
    return slot3;
}

/// Seed the OP_L1_BLOCK fee slots (L1Block.sol layout, slots 1/3/7/8) the fee
/// assembly reads through loadOpFeeParams.
task::Task<void> seedOpFeeParams(
    MutableStorage& storage, uint64_t l1BaseFee, uint32_t baseFeeScalar)
{
    ledger::account::EVMAccount<MutableStorage> acc(storage, opstack::OP_L1_BLOCK, false);
    if (!co_await acc.exists())
    {
        co_await acc.create();
    }
    auto slotKey = [](uint8_t slot) {
        evmc::bytes32 key{};
        key.bytes[31] = slot;
        return key;
    };
    // slot 1: l1_base_fee (whole slot).
    co_await acc.setStorage(slotKey(1), intx::be::store<evmc::bytes32>(intx::uint256{l1BaseFee}));
    // slot 3: base_fee_scalar in bytes [16,20), blob_base_fee_scalar in [20,24).
    co_await acc.setStorage(slotKey(3), packSlot3(baseFeeScalar));
    // slot 7: blob_base_fee = 1.
    co_await acc.setStorage(slotKey(7), intx::be::store<evmc::bytes32>(intx::uint256{1}));
    // slot 8: operator fee scalar/constant and DA footprint scalar all 0.
    co_await acc.setStorage(slotKey(8), evmc::bytes32{});
}

task::Task<u256> readBalance(MutableStorage& storage, evmc_address const& addr)
{
    ledger::account::EVMAccount<MutableStorage> acc(storage, addr, false);
    co_return co_await acc.balance();
}

task::Task<std::string> readNonce(MutableStorage& storage, evmc_address const& addr)
{
    ledger::account::EVMAccount<MutableStorage> acc(storage, addr, false);
    auto nonce = co_await acc.nonce();
    co_return nonce.value_or("<absent>");
}

/// Raw 0x7E envelope: 0x7e || rlp([sourceHash, from, to, mint, value, gas,
/// isSystemTx, data]). `to`/`mint` empty items encode creation / no-mint.
bcos::bytes buildDepositEnvelope(evmc_address const& from, std::optional<evmc_address> to,
    std::optional<uint64_t> mint, uint64_t value, uint64_t gas, bool isSystemTx,
    bcos::bytes const& data)
{
    using namespace bcos::codec::rlp;
    bcos::bytes body;
    encode(body, h256{});  // sourceHash (all-zero is fine for execution)
    encode(body, bcos::Address(bcos::bytesConstRef(from.bytes, sizeof(from.bytes))));
    if (to.has_value())
    {
        encode(body, bcos::Address(bcos::bytesConstRef(to->bytes, sizeof(to->bytes))));
    }
    else
    {
        body.push_back(BYTES_HEAD_BASE);
    }
    if (mint.has_value())
    {
        encode(body, *mint);
    }
    else
    {
        body.push_back(BYTES_HEAD_BASE);
    }
    encode(body, value);
    encode(body, gas);
    encode(body, static_cast<uint64_t>(isSystemTx ? 1 : 0));
    encode(body, data);

    bcos::bytes envelope;
    envelope.push_back(0x7e);
    encodeHeader(envelope, Header{.isList = true, .payloadLength = body.size()});
    envelope.insert(envelope.end(), body.begin(), body.end());
    return envelope;
}

/// A type-2 transfer as the executor sees it: Tars mirror fields (execution
/// inputs) plus the reassemblable signed envelope (extraTransactionBytes +
/// 65-byte signature) the L1 data fee is priced over.
std::shared_ptr<bcostars::protocol::TransactionImpl> makeWeb3TransferTx(evmc_address const& from,
    evmc_address const& to, uint64_t value, uint64_t gasLimit, uint64_t maxFee,
    uint64_t maxPriorityFee)
{
    using namespace bcos::codec::rlp;

    auto tarsTx = std::make_shared<bcostars::Transaction>();
    auto& data = tarsTx->data;
    data.version = 0;
    data.blockLimit = 0;
    data.nonce = "0";
    data.gasLimit = static_cast<int64_t>(gasLimit);
    data.value = intx::hex(intx::uint256{value});
    data.maxFeePerGas = intx::hex(intx::uint256{maxFee});
    data.maxPriorityFeePerGas = intx::hex(intx::uint256{maxPriorityFee});
    data.chainID = "1";
    data.to = bcos::toHexStringWithPrefix(bcos::bytes(std::begin(to.bytes), std::end(to.bytes)));
    tarsTx->type = 1;  // protocol::TransactionType::Web3Transaction
    tarsTx->web3TypedTxKind = 2;
    tarsTx->sender.assign(std::begin(from.bytes), std::end(from.bytes));

    // Signed envelope: the 0x02 signing payload + a synthetic 65-byte signature.
    bcos::bytes body;
    encode(body, static_cast<uint64_t>(1));  // chainId
    encode(body, static_cast<uint64_t>(0));  // nonce
    encode(body, maxPriorityFee);
    encode(body, maxFee);
    encode(body, gasLimit);
    encode(body, bcos::Address(bcos::bytesConstRef(to.bytes, sizeof(to.bytes))));
    encode(body, value);
    encode(body, bcos::bytes{});     // data
    body.push_back(LIST_HEAD_BASE);  // empty accessList
    bcos::bytes payload;
    payload.push_back(0x02);
    encodeHeader(payload, Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());
    tarsTx->extraTransactionBytes.assign(payload.begin(), payload.end());

    bcos::bytes signature(65, 0);
    signature[31] = 0x12;  // r != 0
    signature[63] = 0x34;  // s != 0
    signature[64] = 0x01;  // yParity
    tarsTx->signature.assign(signature.begin(), signature.end());

    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsTx = std::move(tarsTx)]() mutable { return tarsTx.get(); });
}

/// Fault-injection wrapper: forwards every storage2 op to the inner MutableStorage,
/// but once armed the READ entry points throw — simulating a backend I/O failure
/// underneath the noexcept StateView bridge. Writes keep forwarding so the test
/// proves the execution is discarded before any write, not that writes failed.
struct FaultInjectionStorage
{
    MutableStorage& inner;
    bool armed = false;

    // storage2::range is a member-based CPO (Storage.h calls storage.range(...)).
    auto range(auto&&... args) -> task::Task<task::AwaitableReturnType<decltype(storage2::range(
        inner, std::forward<decltype(args)>(args)...))>>
    {
        if (armed)
        {
            throw std::runtime_error("injected storage read failure");
        }
        co_return co_await storage2::range(inner, std::forward<decltype(args)>(args)...);
    }
};

void throwIfArmed(FaultInjectionStorage& storage)
{
    if (storage.armed)
    {
        throw std::runtime_error("injected storage read failure");
    }
}

auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, FaultInjectionStorage& storage,
    ::ranges::input_range auto&& keys)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readSome(
        storage.inner, std::forward<decltype(keys)>(keys)))>>
{
    throwIfArmed(storage);
    co_return co_await storage2::readSome(storage.inner, std::forward<decltype(keys)>(keys));
}

auto tag_invoke(
    storage2::tag_t<storage2::readOne> /*unused*/, FaultInjectionStorage& storage, auto const& key)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readOne(storage.inner, key))>>
{
    throwIfArmed(storage);
    co_return co_await storage2::readOne(storage.inner, key);
}

auto tag_invoke(storage2::tag_t<storage2::existsOne> /*unused*/, FaultInjectionStorage& storage,
    auto const& key) -> task::Task<bool>
{
    throwIfArmed(storage);
    co_return co_await storage2::existsOne(storage.inner, key);
}

auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, FaultInjectionStorage& storage,
    auto&& keyValues) -> task::Task<void>
{
    co_await storage2::writeSome(storage.inner, std::forward<decltype(keyValues)>(keyValues));
}

auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, FaultInjectionStorage& storage,
    auto key, auto value) -> task::Task<void>
{
    co_await storage2::writeOne(storage.inner, std::move(key), std::move(value));
}

auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, FaultInjectionStorage& storage,
    auto&& keys) -> task::Task<void>
{
    co_await storage2::removeSome(storage.inner, std::forward<decltype(keys)>(keys));
}

bcostars::protocol::BlockHeaderImpl makeBlockHeader(bcos::crypto::Hash& hashImpl)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    header.calculateHash(hashImpl);
    return header;
}

ledger::LedgerConfig makeLedgerConfig(bool l2Mode)
{
    ledger::LedgerConfig config;
    config.setEVMCRevision(EVMC_OSAKA);
    config.setGasLimit({30'000'000, 0});
    config.setGasPrice({"0x7", 0});  // block base fee = 7
    if (l2Mode)
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
        config.setFeatures(features);
    }
    return config;
}

void testDepositDecoder()
{
    const auto from = makeAddress(0xcc);
    const auto to = makeAddress(0xdd);

    // Full envelope roundtrip.
    auto envelope = buildDepositEnvelope(from, to, 100, 5, 100000, false, {0x01, 0x02});
    auto decoded = decodeDepositTx(bcos::ref(envelope));
    CHECK(std::holds_alternative<opstack::DepositTx>(decoded));
    if (auto* dep = std::get_if<opstack::DepositTx>(&decoded))
    {
        CHECK(dep->from == from);
        CHECK(dep->to.has_value() && *dep->to == to);
        CHECK(dep->mint.has_value() && *dep->mint == 100);
        CHECK(dep->value == 5);
        CHECK(dep->gas_limit == 100000);
        CHECK(!dep->is_system_tx);
        CHECK(dep->data.size() == 2);
    }

    // Creation + no-mint: empty RLP items.
    auto creation = decodeDepositTx(bcos::ref(
        envelope = buildDepositEnvelope(from, std::nullopt, std::nullopt, 0, 21000, false, {})));
    CHECK(std::holds_alternative<opstack::DepositTx>(creation));
    if (auto* dep = std::get_if<opstack::DepositTx>(&creation))
    {
        CHECK(!dep->to.has_value());
        CHECK(!dep->mint.has_value());
    }

    // Wrong type byte.
    auto notDeposit = bcos::bytes{0x02, 0x01};
    CHECK(std::holds_alternative<std::error_code>(decodeDepositTx(bcos::ref(notDeposit))));

    // Trailing bytes after the RLP list are consensus-rejected.
    auto trailing = buildDepositEnvelope(from, to, 100, 0, 21000, false, {});
    trailing.push_back(0x00);
    CHECK(std::holds_alternative<std::error_code>(decodeDepositTx(bcos::ref(trailing))));

    // Non-canonical integer encodings (op-geth rejects each of these; the shared
    // lenient decode() would wrap/truncate/accept them). Items are 0-based:
    // sourceHash, from, to, mint, value, gas, isSystemTx, data.
    using namespace bcos::codec::rlp;
    auto canonicalItems = [&]() {
        std::vector<bcos::bytes> items(8);
        encode(items[0], h256{});
        encode(items[1], bcos::Address(bcos::bytesConstRef(from.bytes, sizeof(from.bytes))));
        encode(items[2], bcos::Address(bcos::bytesConstRef(to.bytes, sizeof(to.bytes))));
        encode(items[3], static_cast<uint64_t>(100));    // mint
        encode(items[4], static_cast<uint64_t>(0));      // value (canonical zero = 0x80)
        encode(items[5], static_cast<uint64_t>(21000));  // gas
        encode(items[6], static_cast<uint64_t>(0));      // isSystemTx = false
        encode(items[7], bcos::bytes{});                 // data
        return items;
    };
    auto wrapItems = [](std::vector<bcos::bytes> const& items) {
        bcos::bytes body;
        for (auto const& item : items)
        {
            body.insert(body.end(), item.begin(), item.end());
        }
        bcos::bytes out;
        out.push_back(0x7e);
        encodeHeader(out, Header{.isList = true, .payloadLength = body.size()});
        out.insert(out.end(), body.begin(), body.end());
        return out;
    };
    auto expectReject = [&](size_t index, bcos::bytes rawItem) {
        auto items = canonicalItems();
        items[index] = std::move(rawItem);
        auto bad = wrapItems(items);
        CHECK(std::holds_alternative<std::error_code>(decodeDepositTx(bcos::ref(bad))));
    };
    // Positive control: the canonical items decode.
    auto good = wrapItems(canonicalItems());
    CHECK(std::holds_alternative<opstack::DepositTx>(decodeDepositTx(bcos::ref(good))));
    // gas with a leading zero byte (0x82 0x00 0x52).
    expectReject(5, {0x82, 0x00, 0x52});
    // gas payload longer than uint64 (9 bytes).
    expectReject(5, {0x89, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    // value payload longer than 32 bytes (33 bytes, would wrap mod 2^256).
    {
        bcos::bytes wide{0xa1};
        wide.push_back(0x01);
        wide.insert(wide.end(), 32, 0x00);
        expectReject(4, std::move(wide));
    }
    // value zero encoded as inline 0x00 (canonical zero is the empty payload 0x80).
    expectReject(4, {0x00});
    // isSystemTx = 2 (only 0x80/0x01 are canonical bools).
    expectReject(6, {0x02});
}

void testExecuteDeposit()
{
    bcos::crypto::Keccak256 keccak;
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    EthereumExecutor executor{receiptFactory};

    MutableStorage storage;
    const auto from = makeAddress(0xcc);
    const auto to = makeAddress(0xdd);
    task::tbb::syncWait(seedAccount(storage, from, 0, 5));

    auto header = makeBlockHeader(keccak);
    auto ledgerConfig = makeLedgerConfig(true);

    // Mint 1000, transfer 400 of it to `to`.
    auto envelope = buildDepositEnvelope(from, to, 1000, 400, 100000, false, {});
    auto receipt = task::tbb::syncWait(
        executor.executeDeposit(storage, header, bcos::ref(envelope), ledgerConfig));
    CHECK(receipt != nullptr);
    CHECK(receipt->status() == 0);
    CHECK(receipt->gasUsed() == 21000);

    CHECK(task::tbb::syncWait(readBalance(storage, from)) == u256(600));
    CHECK(task::tbb::syncWait(readBalance(storage, to)) == u256(400));
    // Deposit force-increments the depositor nonce.
    CHECK(task::tbb::syncWait(readNonce(storage, from)) == "6");

    // A malformed envelope is a block-level error: it throws and leaves no state.
    auto malformed = envelope;
    malformed.push_back(0x00);
    bool thrown = false;
    try
    {
        task::tbb::syncWait(
            executor.executeDeposit(storage, header, bcos::ref(malformed), ledgerConfig));
    }
    catch (...)
    {
        thrown = true;
    }
    CHECK(thrown);
    CHECK(task::tbb::syncWait(readBalance(storage, from)) == u256(600));
}

/// A storage read failure under the noexcept StateView bridge must NOT persist a
/// result computed against fabricated ("absent / empty") state: the read-failure
/// latch turns it into an exception BEFORE the state diff is applied.
void testReadFailureDiscardsExecution()
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    auto vm = evmc::VM{evmc_create_evmone()};

    const auto from = makeAddress(0xcc);
    const auto to = makeAddress(0xdd);
    MutableStorage inner;
    task::tbb::syncWait(seedAccount(inner, from, 0, 5));
    FaultInjectionStorage storage{inner};

    EthBlockInfo blockInfo{};
    blockInfo.number = 1;
    blockInfo.timestamp = 1;
    blockInfo.gas_limit = 30'000'000;

    auto envelope = buildDepositEnvelope(from, to, 1000, 400, 100000, false, {});

    // Control: unarmed, the wrapper is transparent and the deposit lands.
    auto receipt = task::tbb::syncWait(executeOpStackDeposit(storage, blockInfo, BlockHashLookup{},
        bcos::ref(envelope), vm, /*chainId=*/1234, receiptFactory, 1));
    CHECK(receipt != nullptr);
    CHECK(receipt->status() == 0);
    CHECK(task::tbb::syncWait(readBalance(inner, from)) == u256(600));

    // Armed: every read throws inside the bridge; the execution must be discarded via
    // an exception, with NO dirty state written.
    storage.armed = true;
    bool thrown = false;
    try
    {
        task::tbb::syncWait(executeOpStackDeposit(storage, blockInfo, BlockHashLookup{},
            bcos::ref(envelope), vm, 1234, receiptFactory, 1));
    }
    catch (...)
    {
        thrown = true;
    }
    CHECK(thrown);
    storage.armed = false;
    CHECK(task::tbb::syncWait(readBalance(inner, from)) == u256(600));
    CHECK(task::tbb::syncWait(readBalance(inner, to)) == u256(400));
    CHECK(task::tbb::syncWait(readNonce(inner, from)) == "6");
}

void testOpStackLaneDispatch()
{
    bcos::crypto::Keccak256 keccak;
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};

    const auto sender = makeAddress(0xaa);
    const auto recipient = makeAddress(0xbb);
    const u256 funding = u256(1'000'000'000'000'000'000ULL);  // 1 ETH

    auto runTransfer = [&](bool l2Mode) {
        MutableStorage storage;
        task::tbb::syncWait(seedAccount(storage, sender, funding, 0));
        task::tbb::syncWait(seedOpFeeParams(storage, /*l1BaseFee=*/1'000'000'000,
            /*baseFeeScalar=*/1000));

        EthereumExecutor executor{receiptFactory};
        auto header = makeBlockHeader(keccak);
        auto ledgerConfig = makeLedgerConfig(l2Mode);
        auto tx = makeWeb3TransferTx(sender, recipient, /*value=*/12345,
            /*gasLimit=*/50000, /*maxFee=*/1000, /*maxPriorityFee=*/1);
        auto receipt = task::tbb::syncWait(
            executor.executeTransaction(storage, header, *tx, 0, ledgerConfig, /*call=*/false));
        CHECK(receipt != nullptr);
        CHECK(receipt->status() == 0);
        struct Result
        {
            u256 recipientBalance;
            u256 l1FeeVault;
            u256 baseFeeVault;
            std::string senderNonce;
            u256 gasUsed;
            bcos::bytes envelope;
        };
        return Result{
            .recipientBalance = task::tbb::syncWait(readBalance(storage, recipient)),
            .l1FeeVault = task::tbb::syncWait(readBalance(storage, opstack::OP_L1_FEE_VAULT)),
            .baseFeeVault = task::tbb::syncWait(readBalance(storage, opstack::OP_BASE_FEE_VAULT)),
            .senderNonce = task::tbb::syncWait(readNonce(storage, sender)),
            .gasUsed = receipt->gasUsed(),
            .envelope = signedTxEnvelope(*tx),
        };
    };

    // OP lane: transfer lands, L1 data fee and base fee are routed to the vaults.
    auto op = runTransfer(true);
    CHECK(op.recipientBalance == u256(12345));
    CHECK(op.senderNonce == "1");
    CHECK(op.gasUsed == u256(21000));
    CHECK(!op.envelope.empty());
    // The L1 fee vault credit equals the Fjord+ FastLZ cost of the signed envelope
    // under the seeded OP_L1_BLOCK params — pins that the fee assembly read the
    // seeded slots and routed the exact cost.
    const auto fee =
        opstack::unpackOpFeeParams(intx::be::store<evmc::bytes32>(intx::uint256{1'000'000'000}),
            packSlot3(1000), intx::be::store<evmc::bytes32>(intx::uint256{1}), evmc::bytes32{});
    const auto expectedL1Cost = opstack::computeL1CostFromFlz(fee,
        opstack::flzCompressLen(evmc::bytes_view{op.envelope.data(), op.envelope.size()}),
        opstack::karstConfig());
    CHECK(expectedL1Cost > 0);
    CHECK(op.l1FeeVault == eth::evm::toBcosU256(expectedL1Cost));
    // Base fee vault gets gasUsed * baseFee (OP does not burn the base fee).
    CHECK(op.baseFeeVault == u256(21000 * kBaseFee));

    // Differential control — same transaction, feature off: the pure-Ethereum
    // lane charges no L1 fee and credits no vault.
    auto ethLane = runTransfer(false);
    CHECK(ethLane.recipientBalance == u256(12345));
    CHECK(ethLane.l1FeeVault == u256(0));
    CHECK(ethLane.baseFeeVault == u256(0));
}
}  // namespace

int main()
{
    testDepositDecoder();
    testExecuteDeposit();
    testReadFailureDiscardsExecution();
    testOpStackLaneDispatch();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "TestOpStackExecution: all checks passed\n";
    return 0;
}
