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

#include <bcos-evm/opstack/OpFeeParams.h>
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
#include <opstack-executor/OpBlockSeal.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/Storage2State.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <intx/intx.hpp>
#include <stdexcept>

#include "TestPrinters.h"

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

// A signed EIP-1559 tx calling the L2ToL1MessagePasser (0x4200...0016, OP_L2_TO_L1_MESSAGE_PASSER)
// sendMessage(bytes32) with an all-zero message hash (selector 0xe12c9ca8, chainId 0x2105). Signed
// with the known test privkey (b3_contracts.py PRIVKEY), so the recovered sender is 0x6afa...C693.
inline constexpr char kWithdrawTxEnvelopeHex[] =
    "02f89182210580843b9aca00847735940083030d409442000000000000000000"
    "0000000000000000001680a4e12c9ca800000000000000000000000000000000"
    "00000000000000000000000000000000c001a0d3379d9b67266aeb5c70214c78"
    "521e0507e502ab29d56979b1a83c6bcbf426d8a0267494e0394f7160b64e2015"
    "a9acd35f6df16d42cf0af0d58498e79ce2bb49f4";

// A real signed EIP-7702 set-code tx from the t8n vector isthmus_setcode_7702.json
// (block.transactions[1]._op_raw, chainId 0x2105, sender 0x7e5f...5bdf, authority to=0x1eff...718).
inline constexpr char kSetcodeTxEnvelopeHex[] =
    "04f8cd822105808405f5e100847735940083030d40941eff47bc3a10a45d4b23"
    "0b5d10e37751fe6aa7188080c0f85ef85c82210594c0de000000000000000000"
    "0000000000000000048080a04f2932930bb9cb89e91dcfbbe82525b7d995da2d"
    "a4a351a71386ded6636a0187a07188790945aae98efa664af83d1b67e5e0585e"
    "7b01a60b74664c66841eb8aedd01a03b37a24be0a0db0436c899eb5413541384"
    "0b37e0150c69ba5ac9de6e71f658c7a03cb8039b14d3d9a995fef3894447d497"
    "eab638113ce0d76649c99c85c7889169";

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

/// Isthmus setL1BlockValues calldata (176B, selector 0x098999be), all-zero fields. Used for the
/// C-4 Jovian-activation shape: an Isthmus-length (176B) attributes deposit in a Jovian block.
bcos::bytes makeIsthmusCalldata()
{
    bcos::bytes data(176, 0);
    data[0] = 0x09;
    data[1] = 0x89;
    data[2] = 0x99;
    data[3] = 0xbe;
    return data;
}

/// A real signed EIP-1559 user-tx envelope from the t8n vector fjord_transfer_basic.json
/// (block.transactions[1]._op_raw, chainId 0x2105, sender 0x7e5f...5bdf). The OP path recovers the
/// sender from the signature, so a dummy-sig envelope would recover a garbage sender with no
/// balance; this real one recovers to the sender the fixture seeds.
inline constexpr char kUserTxEnvelopeHex[] =
    "02f9013e822105808405f5e1008477359400830186a094b0b000000000000000"
    "0000000000000000000001880de0b6b3a7640000b8c80479f5f560b988c4ea6f"
    "e8523be93037def43fa29d31cdee175dc41f337b92a83a9ea71774bcebb7ab0b"
    "58cec7eca58ab97d783ca99951ce676ea6d1f5e6171cbf75553673c7ec123290"
    "4e1f829da51f1620b1c9e76ddfa5ff5f5c1ac7e5baabc48f81ca620d9a1b40a7"
    "4cd0e0dc5a18e5d1b6141249666730d8d676be8990dc9d2bed6c54304970f855"
    "fa4a047eef26a75e4caae417010aaf87bb055d86e161a58f751cf56945e79d45"
    "1225aee813f7aa0014e374ac53560d02b6d907225d37323feb36400786e5c001"
    "a0c9162f1f368afac58af73432071c2ce78dfbf3e18efcc5e7f8df97117c899a"
    "b0a07cc63b3b67568aaebd638f60578cf3aa200f3123e61f7c6cb9589b08da91"
    "9199";

/// Build a deposit envelope (0x7e + RLP list) carrying @p data as the L1-attributes calldata.
bcos::bytes makeDepositEnvelope(bcos::bytes data, uint64_t gas = 1000000)
{
    bcos::bytes envelope;
    envelope.push_back(0x7e);
    const bcos::bytes sourceHash(32, 0x01);
    const bcos::bytes kFrom{0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde,
        0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0x00, 0x01};
    const bcos::bytes kTo{0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15};
    const uint64_t kMint = 0, kValue = 0, kIsSystemTx = 0;
    bcos::codec::rlp::encode(
        envelope, sourceHash, kFrom, kTo, kMint, kValue, gas, kIsSystemTx, data);
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

// Item 2: the deposit writes the fee slots, and the executor's consumer side (loadOpFeeParams)
// reads exactly those values. Closes the deposit -> fee-loop that the t8n vectors only pre-seed.
BOOST_AUTO_TEST_CASE(DepositWritesFeeParamsReadableByLoadOpFeeParams)
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
        bcos::ledger::account::EVMAccount<ViewType> acc(
            view, bcos::evm::opstack::OP_L1_BLOCK, false);
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

    // The consumer side reads the deposit-written slots exactly as unpackOpFeeParams specified.
    bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
    const auto fee = bcos::evm::opstack::loadOpFeeParams(bridge);
    BOOST_CHECK_EQUAL(fee.l1_base_fee, intx::uint256{0x63});
    BOOST_CHECK_EQUAL(fee.base_fee_scalar, 7u);
    BOOST_CHECK_EQUAL(fee.blob_base_fee_scalar, 9u);
    BOOST_CHECK_EQUAL(fee.blob_base_fee, intx::uint256{0x64});
    BOOST_CHECK_EQUAL(fee.operator_fee_scalar, 11u);
    BOOST_CHECK_EQUAL(fee.operator_fee_constant, 13u);
    BOOST_CHECK_EQUAL(fee.da_footprint_gas_scalar, 15u);
}

// Item 3: a block whose L1-attributes deposit fails validation (intrinsic gas too low) still
// seals — op-geth allows failed deposits. The receipt carries status=failure, gasUsed = gasLimit
// in full, and the depositor's nonce is force-incremented (Regolith failed-deposit branch).
BOOST_AUTO_TEST_CASE(FailedDepositSealsBlockWithFullGasAndBumpedNonce)
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
        bcos::ledger::account::EVMAccount<ViewType> acc(
            view, bcos::evm::opstack::OP_L1_BLOCK, false);
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

    // The Jovian L1-attributes calldata (178B) has intrinsic ~21832; gas_limit 20000 is too low
    // -> INTRINSIC_GAS_TOO_LOW -> failed-deposit branch: status=failure, gasUsed = gasLimit,
    // nonce force-incremented (op-geth state_transition.go:486-513).
    constexpr uint64_t kTooLowGas = 20000;
    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeJovianCalldataNonZero(), kTooLowGas)};

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
    BOOST_CHECK_EQUAL(receipt->status(), 1);  // toFiscoStatus: non-SUCCESS -> 1
    BOOST_CHECK_EQUAL(receipt->gasUsed(), bcos::u256{kTooLowGas});  // full gasLimit charged

    // Regolith: the depositor's nonce is force-incremented despite the failure.
    bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_DEPOSITOR, false);
    const auto nonce = bcos::task::syncWait(acc.nonce());
    BOOST_REQUIRE(nonce.has_value());
    BOOST_CHECK_EQUAL(*nonce, std::string{"1"});
}

// B6: fork-boundary / Jovian L1-attributes block-shape rules (op-geth rollup_cost.go C-3/C-4).
//   C-3: a normal Jovian block's attributes deposit must be >= 178B with the Jovian selector.
//   C-4: a block whose attributes deposit is Isthmus-length (176B) is the Jovian *activation*
//        block and must be deposits-only.
// The rules are checked BEFORE the tx loop (validateJovianBlockShape), so a violating block
// throws OpConsensusError -> executeOpBlock throws a runtime_error subclass.
namespace
{
// Shared harness: seeded L1Block code + scheduler; returns (view, scheduler) for one block.
struct JovianShapeFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    ViewType view = multiLayerStorage.fork();
    bcos::evm::engine::OpSchedulerImpl<ViewType, MLS> scheduler;

    JovianShapeFixture()
      : scheduler([] {
            auto cs = std::make_shared<bcos::crypto::CryptoSuite>(
                std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
            bcos::protocol::TransactionReceiptFactory::Ptr rf{
                std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cs)};
            return rf;
        }(), 0x2105,  // chainId of the real user-tx fixture (fjord_transfer_basic.json)
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 1000, .jovianTime = 2000},
            nullptr, multiLayerStorage, {})
    {
        view.newMutable();
        bcos::bytes code = bcos::fromHex(kL1BlockCodeHex);
        const auto codeHash = keccak256(code);
        bcos::task::syncWait([&]() -> bcos::task::Task<void> {
            bcos::ledger::account::EVMAccount<ViewType> acc(view, bcos::evm::opstack::OP_L1_BLOCK, false);
            co_await acc.create();
            co_await acc.setCode(code, /*abi=*/"", codeHash);
            co_await acc.setNonce("1");
            // The user-tx fixture sender (fjord_transfer_basic.json) needs balance for the
            // normal-block test where the tx actually executes.
            const auto kUserSender =
                evmc::from_hex<evmc::address>("7e5f4552091a69125d5dfcb7b8c2659029395bdf").value();
            bcos::ledger::account::EVMAccount<ViewType> usr(view, kUserSender, false);
            co_await usr.create();
            co_await usr.setBalance(bcos::u256("1000000000000000000000"));  // 1000 ETH
            co_await usr.setNonce("0");
            co_await usr.setCode({}, "",
                bcos::crypto::HashType(
                    "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
            co_return;
        }());
    }

    // timestampMillis must put the block in the Jovian config (>= jovianTime*1000).
    bcos::evm::engine::OpExecuteBlockResult run(std::vector<bcos::bytes> rawTxs, int64_t timestampMillis)
    {
        auto header = makeOpHeader(1, timestampMillis);
        return bcos::task::syncWait(scheduler.executeOpBlock(view, *header, rawTxs));
    }
};
}  // namespace

// C-4 happy path: a Jovian activation block (Isthmus-length 176B attributes) with no user tx
// must seal.
BOOST_AUTO_TEST_CASE(JovianActivationDepositOnlySeals)
{
    JovianShapeFixture fx;
    BOOST_REQUIRE_NO_THROW(fx.run(
        {makeDepositEnvelope(makeIsthmusCalldata())}, static_cast<int64_t>(2000) * 1000 + 1000));
}

// C-4 violation: an Isthmus-length attributes deposit followed by a user tx in a Jovian block
// must be rejected (op-geth rollup_cost.go:568-576, deposits-only activation block).
BOOST_AUTO_TEST_CASE(JovianActivationWithUserTxRejected)
{
    JovianShapeFixture fx;
    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeIsthmusCalldata()),
        bcos::fromHex(kUserTxEnvelopeHex)};
    BOOST_CHECK_THROW(fx.run(rawTxs, static_cast<int64_t>(2000) * 1000 + 1000), std::runtime_error);
}

// C-3: a normal Jovian block (178B Jovian attributes) + a user tx must seal.
BOOST_AUTO_TEST_CASE(JovianNormalBlockWithUserTxSeals)
{
    JovianShapeFixture fx;
    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeJovianCalldataNonZero()),
        bcos::fromHex(kUserTxEnvelopeHex)};
    try
    {
        fx.run(rawTxs, static_cast<int64_t>(2000) * 1000 + 1000);
    }
    catch (const std::exception& e)
    {
        BOOST_FAIL("Jovian normal block with user tx threw: " << e.what());
    }
}

// C-3 violation: Jovian attributes shorter than 178B (not Isthmus-length either) -> rejected.
BOOST_AUTO_TEST_CASE(JovianShortAttributesRejected)
{
    JovianShapeFixture fx;
    bcos::bytes shortData = makeJovianCalldataNonZero();
    shortData.resize(170);
    BOOST_CHECK_THROW(fx.run({makeDepositEnvelope(shortData)}, static_cast<int64_t>(2000) * 1000 + 1000),
        std::runtime_error);
}

// C-3 violation: 178B but with the wrong selector -> rejected.
BOOST_AUTO_TEST_CASE(JovianWrongSelectorRejected)
{
    JovianShapeFixture fx;
    bcos::bytes wrong = makeJovianCalldataNonZero();
    wrong[0] = 0x00;  // corrupt the selector
    wrong[1] = 0x00;
    wrong[2] = 0x00;
    wrong[3] = 0x00;
    BOOST_CHECK_THROW(fx.run({makeDepositEnvelope(wrong)}, static_cast<int64_t>(2000) * 1000 + 1000),
        std::runtime_error);
}

// Isthmus-config block (timestamp < jovianTime): a 176B Isthmus deposit is a normal block shape
// (has_da_footprint=false -> C-3/C-4 do not apply) and seals.
BOOST_AUTO_TEST_CASE(IsthmusActivationBlockSeals)
{
    JovianShapeFixture fx;
    BOOST_REQUIRE_NO_THROW(fx.run(
        {makeDepositEnvelope(makeIsthmusCalldata())}, static_cast<int64_t>(1000) * 1000 + 500));
}

// ---- Item 5: invalid-block rejections (op-geth state_processor.go block-level errors) ----

// First tx is not the L1 attributes deposit -> rejected outright.
BOOST_AUTO_TEST_CASE(FirstTxNotAttributesRejected)
{
    JovianShapeFixture fx;
    BOOST_CHECK_THROW(
        fx.run({bcos::fromHex(kUserTxEnvelopeHex)}, static_cast<int64_t>(2000) * 1000 + 1000),
        std::runtime_error);
}

// A deposit appearing after a non-deposit tx -> rejected (stricter-than-spec guard).
BOOST_AUTO_TEST_CASE(DepositAfterNonDepositRejected)
{
    JovianShapeFixture fx;
    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeJovianCalldataNonZero()),
        bcos::fromHex(kUserTxEnvelopeHex), makeDepositEnvelope(makeJovianCalldataNonZero())};
    BOOST_CHECK_THROW(fx.run(rawTxs, static_cast<int64_t>(2000) * 1000 + 1000), std::runtime_error);
}

// A deposit whose gasLimit exceeds the remaining block gas -> rejected (block error).
BOOST_AUTO_TEST_CASE(GasLimitOverBlockBudgetRejected)
{
    JovianShapeFixture fx;
    BOOST_CHECK_THROW(
        fx.run({makeDepositEnvelope(makeJovianCalldataNonZero(), 4'000'000'000)},
            static_cast<int64_t>(2000) * 1000 + 1000),
        std::runtime_error);
}

// ---- Item 4: MessagePasser storage drives the withdrawal root (opStorageRoot, OpBlockSeal) ----
// OpBlockSeal has no direct unit test; the withdrawal root is consensus-critical (op-geth derives
// it from the L2ToL1MessagePasser storage after the block). Seed the MessagePasser with known
// slots, run a block that does not touch it, and assert the seal's withdrawalsRoot equals
// opStorageRoot over the same slots.
BOOST_AUTO_TEST_CASE(MessagePasserStorageDrivesWithdrawalRoot)
{
    using namespace evmc::literals;
    JovianShapeFixture fx;

    // Seed the MessagePasser (0x4200...11) with two non-zero slots.
    const auto kPasser = bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER;
    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        bcos::ledger::account::EVMAccount<ViewType> acc(fx.view, kPasser, false);
        co_await acc.create();
        co_await acc.setNonce("1");
        evmc::bytes32 k1{};
        k1.bytes[31] = 0x01;
        evmc::bytes32 v1{};
        v1.bytes[31] = 0x02;
        evmc::bytes32 k2{};
        k2.bytes[31] = 0x02;
        evmc::bytes32 v2{};
        v2.bytes[31] = 0x03;
        co_await acc.setStorage(k1, v1);
        co_await acc.setStorage(k2, v2);
        co_return;
    }());

    auto result = fx.run({makeDepositEnvelope(makeJovianCalldataNonZero())},
        static_cast<int64_t>(2000) * 1000 + 1000);
    BOOST_REQUIRE_EQUAL(result.receipts.size(), 1u);

    // The expected root is opStorageRoot over the seeded slots (the block does not touch the
    // MessagePasser). Zero-value slots are skipped by opStorageRoot.
    std::map<evmc::bytes32, evmc::bytes32> seeded;
    evmc::bytes32 k1{};
    k1.bytes[31] = 0x01;
    evmc::bytes32 v1{};
    v1.bytes[31] = 0x02;
    evmc::bytes32 k2{};
    k2.bytes[31] = 0x02;
    evmc::bytes32 v2{};
    v2.bytes[31] = 0x03;
    seeded[k1] = v1;
    seeded[k2] = v2;
    const auto expected = bcos::evm::opstack::opStorageRoot(seeded);
    BOOST_TEST_MESSAGE("seal withdrawalsRoot: 0x"
                       << evmc::hex(evmc::bytes_view(result.seal.withdrawalsRoot.bytes,
                                              sizeof(result.seal.withdrawalsRoot.bytes))));
    BOOST_TEST_MESSAGE("expected opStorageRoot: 0x"
                       << evmc::hex(evmc::bytes_view(expected.bytes, sizeof(expected.bytes))));
    BOOST_CHECK_EQUAL(std::memcmp(result.seal.withdrawalsRoot.bytes, expected.bytes,
                          sizeof(expected.bytes)),
        0);
}

// ---- Item 6: EIP-7702 set-code tx in a block (deposit + setcode) ----
// A real signed set-code tx (isthmus_setcode_7702.json) delegating authority 0x1eff...718 to
// implementation 0xc0de...04 must write the delegation designator (0xef0100 ++ impl) onto the
// authority in the block's post-state.
BOOST_AUTO_TEST_CASE(SetCode7702InBlockWritesDelegation)
{
    using namespace evmc::literals;
    JovianShapeFixture fx;

    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeJovianCalldataNonZero()),
        bcos::fromHex(kSetcodeTxEnvelopeHex)};
    auto result = fx.run(rawTxs, static_cast<int64_t>(2000) * 1000 + 1000);
    BOOST_REQUIRE_EQUAL(result.receipts.size(), 2u);
    BOOST_CHECK_EQUAL(result.receipts[1]->status(), 0);

    // The authority (tx.to = 0x1eff...) now carries the delegation designator 0xef0100 ++ impl.
    const auto authority =
        evmc::from_hex<evmc::address>("1eff47bc3a10a45d4b230b5d10e37751fe6aa718").value();
    const auto impl = evmc::from_hex<evmc::address>("c0de000000000000000000000000000000000004").value();
    bcos::ledger::account::EVMAccount<ViewType> acc(fx.view, authority, false);
    auto codeEntry = bcos::task::syncWait(acc.code());
    BOOST_REQUIRE(codeEntry.has_value());
    const auto& code = codeEntry->get();
    BOOST_REQUIRE_GE(code.size(), 23u);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(code[0]), 0xef);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(code[1]), 0x01);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(code[2]), 0x00);
    BOOST_CHECK_EQUAL(std::memcmp(code.data() + 3, impl.bytes, sizeof(impl.bytes)), 0);
}

// ---- Item ③: transaction-driven withdrawal flow ----
// A user tx calling L2ToL1MessagePasser.sendMessage(bytes32) writes sentMessages[hash]=true (the
// Solidity mapping slot keccak256(hash || uint256(0))), and the block's withdrawal root changes
// to reflect the new slot. Uses a FRESH view (like the L1Block tests) rather than the shared
// JovianShapeFixture, whose MLS layer visibility made EVMAccount's writes unreachable by the
// bridge (fixture quirk, not a production issue).
BOOST_AUTO_TEST_CASE(WithdrawTxWritesMessagePasserAndChangesRoot)
{
    using namespace evmc::literals;
    using intx::operator""_u256;

    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    constexpr uint64_t kIsthmusTime = 1000;
    constexpr uint64_t kJovianTime = 2000;

    const auto kPasser = bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER;
    const auto kSender =
        evmc::from_hex<evmc::address>("6afa9580383e6627da926b6f6ed9ab2b9c8cc693").value();
    // PUSH1 1 (value) CALLDATACOPY(dest=0,offset=4,size=32) MSTORE(32,0) KECCAK256(offset=0,size=64)
    // SSTORE RETURN. EVM stack args are TOP-first: CALLDATACOPY pops dest,offset,size so push
    // size,offset,dest; KECCAK256 pops offset,size so push size,offset; value pushed FIRST so
    // SSTORE pops key=slot then value=1.
    bcos::bytes passerCode = bcos::fromHex("600160206004600037600060205260406000205560006000f3");
    const auto passerCodeHash = keccak256(passerCode);
    bcos::bytes l1Code = bcos::fromHex(kL1BlockCodeHex);
    const auto l1CodeHash = keccak256(l1Code);

    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        bcos::ledger::account::EVMAccount<ViewType> l1(view, bcos::evm::opstack::OP_L1_BLOCK, false);
        co_await l1.create();
        co_await l1.setCode(l1Code, /*abi=*/"", l1CodeHash);
        co_await l1.setNonce("1");
        bcos::ledger::account::EVMAccount<ViewType> mp(view, kPasser, false);
        co_await mp.create();
        co_await mp.setCode(passerCode, /*abi=*/"", passerCodeHash);
        co_await mp.setNonce("1");
        bcos::ledger::account::EVMAccount<ViewType> snd(view, kSender, false);
        co_await snd.create();
        co_await snd.setBalance(bcos::u256("1000000000000000000000"));  // 1000 ETH
        co_await snd.setNonce("0");
        co_await snd.setCode({}, "",
            bcos::crypto::HashType(
                "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        co_return;
    }());

    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)};
    bcos::evm::engine::OpSchedulerImpl<ViewType, MLS> scheduler(receiptFactory, 0x2105,
        bcos::evm::opstack::OpForkTimestamps{
            .isthmusTime = kIsthmusTime, .jovianTime = kJovianTime},
        nullptr, multiLayerStorage, {});

    auto header = makeOpHeader(1, static_cast<int64_t>(kJovianTime) * 1000 + 1000);
    std::vector<bcos::bytes> rawTxs{makeDepositEnvelope(makeJovianCalldataNonZero()),
        bcos::fromHex(kWithdrawTxEnvelopeHex)};
    auto result = bcos::task::syncWait(scheduler.executeOpBlock(view, *header, rawTxs));
    BOOST_REQUIRE_EQUAL(result.receipts.size(), 2u);
    BOOST_CHECK_EQUAL(result.receipts[1]->status(), 0);
    // The withdraw tx wrote sentMessages[0x00..00] = true: slot keccak256(0x00*64) = 1.
    bcos::bytes slotInput(64, 0);
    auto slotHash = keccak256(slotInput);
    evmc::bytes32 slotKey{};
    std::copy_n(slotHash.data(), 32, slotKey.bytes);
    bcos::ledger::account::EVMAccount<ViewType> mp(view, kPasser, false);
    const auto slotVal = bcos::task::syncWait(mp.storage(slotKey));
    BOOST_CHECK_EQUAL(slotVal.bytes[31], 0x01);


    // The withdrawal root now reflects the one non-zero slot.
    std::map<evmc::bytes32, evmc::bytes32> expectedStorage;
    evmc::bytes32 one{};
    one.bytes[31] = 0x01;
    expectedStorage[slotKey] = one;
    const auto expectedRoot = bcos::evm::opstack::opStorageRoot(expectedStorage);
    BOOST_CHECK_EQUAL(std::memcmp(result.seal.withdrawalsRoot.bytes, expectedRoot.bytes,
                          sizeof(expectedRoot.bytes)),
        0);
    // And it is NOT the empty-trie root (the sendMessage changed the state).
    const auto emptyRoot = bcos::ledger::mpt::emptyRootHash();
    BOOST_CHECK_NE(std::memcmp(result.seal.withdrawalsRoot.bytes, emptyRoot.data(),
                       bcos::h256::SIZE),
        0);
}

BOOST_AUTO_TEST_SUITE_END()
