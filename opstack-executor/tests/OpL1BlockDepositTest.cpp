// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpL1BlockDepositTest — offline reproduction of the B3a node's L1Block deposit behaviour.
//
// The real node: genesis seeds L1Block (0x4200...15) with code + slot1=0x1234, yet the L1
// attributes deposit executes with receipt status=0 but does NOT write slot1 (stays 0x1234),
// with gasUsed=23080 == the "skip trivial execution" signature (empty code). Every read-path
// gate for the code is satisfied in the DB (SYS_TABLES / CODE_HASH / SYS_CODE_BINARY), so the
// disconnect must be reproduced offline through the same bridge the node uses
// (Storage2State over MultiLayerStorage + runDeposit via executeOpBlock).
//
// This test seeds L1Block EXACTLY as importGenesisState does (EVMAccount create + setCode +
// setStorage + setNonce), then runs the exact block-1 deposit envelope through
// OpSchedulerImpl::executeOpBlock, then reads slot1.
//
//   - if slot1 is written (0) -> the offline bridge path works; the node's issue is in its
//     runtime storage fork, not the executor.
//   - if slot1 stays 0x1234 -> the bug is REPRODUCED offline; the executor/bridge is at fault.

#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/Storage2State.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <stdexcept>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) & { std::abort(); }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

// The 146-byte L1Block runtime bytecode (tools/op-e2e/gen_l1block.py): dispatches on
// setL1BlockValues (Isthmus 0x098999be / Jovian 0x3db6be2b), writes slots 1/3/7/8 aligned to
// FISCO's unpackOpFeeParams reads, returns.
inline constexpr char kL1BlockCodeHex[] =
    "6004361060255760003560e01c63098999be14602b5760003560e01c633d"
    "b6be2b14602b575b60006000fd5b6000358060c01c63ffffffff1660601b"
    "60003560a01c63ffffffff1660401b176003555060243560015560443560"
    "075560a03560c01c63ffffffff1660401b60a03560801c67ffffffffffff"
    "ffff161760b03560f01c61ffff1660601b1760085560006000f3";// Block 1's exact L1 attributes deposit envelope (type 0x7e, to=OP_L1_BLOCK, gas=0xf4240,
// 178-byte Jovian calldata 0x3db6be2b...) captured from the B3a node.
inline constexpr char kDepositEnvelopeHex[] =
    "7ef90106a05eea6d70f9bde6d282e117c76b5da51b2b5b5aa4040c6481df4156"
    "5df07361fc94deaddeaddeaddeaddeaddeaddeaddeaddead0001944200000000"
    "0000000000000000000000000000158080830f424080b8b23db6be2b00000000"
    "000000000000000000000001000000006a7c4891000000000000000100000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000";

std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeOpHeader(
    bcos::protocol::BlockNumber number, int64_t timestampMillis)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(3'000'000'000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(0));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

bcos::crypto::HashType keccak256(const bcos::bytes& data)
{
    return bcos::crypto::Keccak256{}.hash(bcos::ref(data));
}

/// Write @p value big-endian into @p data at [offset, offset+width).
void putBe(bcos::bytes& data, size_t offset, uint64_t value, size_t width)
{
    for (size_t i = 0; i < width; ++i)
        data[offset + width - 1 - i] = static_cast<bcos::byte>((value >> (i * 8)) & 0xFF);
}

/// Jovian setL1BlockValues calldata (178B, selector 0x3db6be2b) with NON-ZERO fields, aligned to
/// makeL1AttributesDeposit's layout (OpEngineSeam.h): [4:8] baseFeeScalar, [8:12] blobBaseFeeScalar,
/// [12:20] seq, [20:28] ts, [28:36] num, [36:68] l1BaseFee, [68:100] blobBaseFee, [100:132] l1Hash,
/// [132:164] batcherHash, [164:168] opFeeScalar, [168:176] opFeeConstant, [176:178] da.
bcos::bytes makeJovianCalldataNonZero()
{
    bcos::bytes data(178, 0);
    data[0] = 0x3d;
    data[1] = 0xb6;
    data[2] = 0xbe;
    data[3] = 0x2b;
    putBe(data, 4, 0x00000007, 4);      // baseFeeScalar = 7
    putBe(data, 8, 0x00000009, 4);      // blobBaseFeeScalar = 9
    putBe(data, 12, 0x1111111111111111, 8);  // sequenceNumber
    putBe(data, 20, 0x6a7c4891, 8);     // timestamp
    putBe(data, 28, 1, 8);              // blockNumber
    putBe(data, 36, 0x63, 32);          // l1BaseFee = 0x63 = 99
    putBe(data, 68, 0x64, 32);          // blobBaseFee = 0x64 = 100
    putBe(data, 164, 0x0000000b, 4);    // opFeeScalar = 11
    putBe(data, 168, 0x000000000000000d, 8);  // opFeeConstant = 13
    putBe(data, 176, 0x000f, 2);        // daFootprintGasScalar = 15
    return data;
}

/// Build a deposit envelope (0x7e + RLP list) carrying @p data as the L1-attributes calldata.
bcos::bytes makeDepositEnvelope(bcos::bytes data)
{
    bcos::bytes envelope;
    envelope.push_back(0x7e);
    const bcos::bytes sourceHash(32, 0x01);
    const bcos::bytes kFrom{0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde,
        0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0x00, 0x01};
    const bcos::bytes kTo{0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15};
    const uint64_t kMint = 0, kValue = 0, kGas = 1000000, kIsSystemTx = 0;
    bcos::codec::rlp::encode(
        envelope, sourceHash, kFrom, kTo, kMint, kValue, kGas, kIsSystemTx, data);
    return envelope;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpL1BlockDepositSuite)

BOOST_AUTO_TEST_CASE(L1BlockDepositWritesSlots)
{
    using namespace evmc::literals;

    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    constexpr uint64_t kIsthmusTime = 1000;
    constexpr uint64_t kJovianTime = 2000;

    // ---- Seed L1Block exactly as importGenesisState does (EVMAccount create+setCode+setStorage) --
    bcos::bytes code = bcos::fromHex(kL1BlockCodeHex);
    const auto codeHash = keccak256(code);
    BOOST_TEST_MESSAGE("seeded L1Block code len=" << code.size()
                                                 << " codeHash=" << codeHash.hex());

    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_L1_BLOCK, false);
        co_await acc.create();
        co_await acc.setCode(code, /*abi=*/"", codeHash);
        co_await acc.setNonce("1");
        // slot1 = 0x1234 (the genesis seed).
        evmc::bytes32 slot1Key{};
        slot1Key.bytes[31] = 0x01;
        evmc::bytes32 slot1Value{};
        slot1Value.bytes[30] = 0x12;
        slot1Value.bytes[31] = 0x34;
        co_await acc.setStorage(slot1Key, slot1Value);
        co_return;
    }());

    // Sanity: the seeded account is visible through EVMAccount AND through the Storage2State
    // bridge the deposit actually reads via (get_account -> code_hash gate, get_account_code ->
    // CODE_HASH -> SYS_CODE_BINARY).
    {
        bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_L1_BLOCK, false);
        BOOST_CHECK(bcos::task::syncWait(acc.exists()));
        auto c = bcos::task::syncWait(acc.code());
        BOOST_REQUIRE(c.has_value());
        BOOST_CHECK_EQUAL(c->get().size(), code.size());

        bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
        const auto acc0 = bridge.get_account(bcos::evm::opstack::OP_L1_BLOCK);
        BOOST_REQUIRE(acc0.has_value());
        const auto loadedCode = bridge.get_account_code(bcos::evm::opstack::OP_L1_BLOCK);
        BOOST_TEST_MESSAGE("bridge get_account_code size=" << loadedCode.size());
        BOOST_CHECK_EQUAL(loadedCode.size(), code.size());
        evmc::bytes32 slot1Key{};
        slot1Key.bytes[31] = 0x01;
        const auto s1 = bridge.get_storage(bcos::evm::opstack::OP_L1_BLOCK, slot1Key);
        BOOST_TEST_MESSAGE("bridge get_storage slot1=0x"
                           << evmc::hex(evmc::bytes_view(s1.bytes, sizeof(s1.bytes))));
    }

    // ---- Run the deposit block through the real OpSchedulerImpl bridge ----
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)};
    bcos::evm::engine::OpSchedulerImpl<ViewType, MLS> scheduler(
        receiptFactory, /*chainId=*/11155111,
        bcos::evm::opstack::OpForkTimestamps{
            .isthmusTime = kIsthmusTime, .jovianTime = kJovianTime},
        /*blockFactory=*/nullptr, multiLayerStorage, {});

    // Jovian-active timestamp (>= jovianTime); the deposit calldata is 178B with the Jovian
    // selector, satisfying validateJovianBlockShape.
    auto header = makeOpHeader(1, static_cast<int64_t>(kJovianTime) * 1000 + 1000);
    std::vector<bcos::bytes> rawTxs;
    rawTxs.emplace_back(bcos::fromHex(kDepositEnvelopeHex));

    bcos::evm::engine::OpExecuteBlockResult result;
    try
    {
        result = bcos::task::syncWait(scheduler.executeOpBlock(view, *header, rawTxs));
    }
    catch (const std::exception& e)
    {
        BOOST_FAIL("executeOpBlock threw: " << e.what());
    }

    BOOST_REQUIRE_EQUAL(result.receipts.size(), 1u);
    const auto& receipt = result.receipts.front();
    BOOST_TEST_MESSAGE("deposit receipt status=" << receipt->status()
                                                 << " gasUsed=" << receipt->gasUsed().str());
    // FISCO convention: 0 == success (EVMC_SUCCESS).
    BOOST_CHECK_EQUAL(receipt->status(), 0);

    // ---- The money question: what did the deposit's L1Block call actually observe? ----
    // slot1 = l1_base_fee = 0 (this deposit's l1_base_fee is 0) — the real L1Block code writes
    // it once the size-guard condition is correct (EVM LT is `top < second`; the guard must be
    // `PUSH1 4 CALLDATASIZE LT` = cd < 4, not `CALLDATASIZE PUSH1 4 LT` = cd > 4).
    evmc::bytes32 slot1Key{};
    slot1Key.bytes[31] = 0x01;
    bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_L1_BLOCK, false);
    const auto slot1 = bcos::task::syncWait(acc.storage(slot1Key));
    BOOST_TEST_MESSAGE("L1Block slot1 after deposit: 0x"
                       << evmc::hex(evmc::bytes_view(slot1.bytes, sizeof(slot1.bytes))));
    evmc::bytes32 expectedZero{};
    BOOST_CHECK_EQUAL(std::memcmp(slot1.bytes, expectedZero.bytes, sizeof(slot1.bytes)), 0);
}

// Item 1+2: a deposit whose L1 attributes are NON-ZERO must write slot1/3/7/8 with values that
// round-trip through unpackOpFeeParams (OpFeeParams.cpp) — i.e. the bytecode's slot layout must
// match what the executor consumes: l1_base_fee = ENTIRE slot1, base_fee_scalar = slot3[16:20],
// blob_base_fee_scalar = slot3[20:24], blob_base_fee = ENTIRE slot7, operator_fee_scalar =
// slot8[20:24], operator_fee_constant = slot8[24:32], da_footprint = slot8[18:20].
BOOST_AUTO_TEST_CASE(NonZeroL1ParamsAlignWithUnpackOpFeeParams)
{
    using namespace evmc::literals;

    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    constexpr uint64_t kIsthmusTime = 1000;
    constexpr uint64_t kJovianTime = 2000;

    bcos::bytes code = bcos::fromHex(kL1BlockCodeHex);
    const auto codeHash = keccak256(code);
    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_L1_BLOCK, false);
        co_await acc.create();
        co_await acc.setCode(code, /*abi=*/"", codeHash);
        co_await acc.setNonce("1");
        co_return;
    }());

    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)};
    bcos::evm::engine::OpSchedulerImpl<ViewType, MLS> scheduler(receiptFactory, 11155111,
        bcos::evm::opstack::OpForkTimestamps{
            .isthmusTime = kIsthmusTime, .jovianTime = kJovianTime},
        nullptr, multiLayerStorage, {});

    auto header = makeOpHeader(1, static_cast<int64_t>(kJovianTime) * 1000 + 1000);
    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeJovianCalldataNonZero())};

    bcos::evm::engine::OpExecuteBlockResult result;
    try
    {
        result = bcos::task::syncWait(scheduler.executeOpBlock(view, *header, rawTxs));
    }
    catch (const std::exception& e)
    {
        BOOST_FAIL("executeOpBlock threw: " << e.what());
    }
    BOOST_REQUIRE_EQUAL(result.receipts.size(), 1u);
    BOOST_CHECK_EQUAL(result.receipts.front()->status(), 0);

    // Expected slot values, derived from makeL1AttributesDeposit's field layout and
    // unpackOpFeeParams' read offsets:
    //   slot1 = l1_base_fee = calldata[36:68] = 0x63 (99)
    //   slot3 = (baseFeeScalar << 96) | (blobBaseFeeScalar << 64)
    //         = bytes[16:20] = 0x00000007, bytes[20:24] = 0x00000009
    //   slot7 = blob_base_fee = calldata[68:100] = 0x64 (100)
    //   slot8 = (da << 96) | (opFeeScalar << 64) | opFeeConstant
    //         = bytes[18:20] = 0x000f, bytes[20:24] = 0x0000000b, bytes[24:32] = 0x000000000000000d
    evmc::bytes32 expectedSlot1{};
    expectedSlot1.bytes[31] = 0x63;
    evmc::bytes32 expectedSlot3{};
    expectedSlot3.bytes[19] = 0x07;
    expectedSlot3.bytes[23] = 0x09;
    evmc::bytes32 expectedSlot7{};
    expectedSlot7.bytes[31] = 0x64;
    evmc::bytes32 expectedSlot8{};
    expectedSlot8.bytes[19] = 0x0f;
    expectedSlot8.bytes[23] = 0x0b;
    expectedSlot8.bytes[31] = 0x0d;

    bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_L1_BLOCK, false);
    auto checkSlot = [&](int slotNum, const evmc::bytes32& expected) {
        evmc::bytes32 key{};
        key.bytes[31] = static_cast<uint8_t>(slotNum);
        const auto val = bcos::task::syncWait(acc.storage(key));
        BOOST_TEST_MESSAGE("slot" << slotNum << " = 0x"
                                  << evmc::hex(evmc::bytes_view(val.bytes, sizeof(val.bytes))));
        BOOST_CHECK_EQUAL(std::memcmp(val.bytes, expected.bytes, sizeof(val.bytes)), 0);
    };
    checkSlot(1, expectedSlot1);
    checkSlot(3, expectedSlot3);
    checkSlot(7, expectedSlot7);
    checkSlot(8, expectedSlot8);
}

BOOST_AUTO_TEST_SUITE_END()
