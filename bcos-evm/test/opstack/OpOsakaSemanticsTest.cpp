#include "OpPredeploysSeed.h"
#include "OpTestReceiptFactory.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include "support/KarstScheduleFixtures.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <evmone/evmone.h>
#include <json/json.h>
#include <openssl/sha.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <bcos-evm/eth/state/state.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
// Unity-build: OpHostTest already owns makeBlock/kSender/kP256 in an anonymous namespace.
constexpr auto kOsakaSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kOsakaContract = 0x00000000000000000000000000000000000000bb_address;
constexpr auto kOsakaModExp = 0x0000000000000000000000000000000000000005_address;
constexpr auto kOsakaP256 = 0x0000000000000000000000000000000000000100_address;
constexpr auto kOsakaBn256Pairing = 0x0000000000000000000000000000000000000008_address;
constexpr size_t kBn256PairSize = 192;
// EIP-7823 bounds each modexp length field at INPUT_SIZE_LIMIT; 1025 is the first illegal
// value. One definition: the bound tests below must move together if the spec changes.
constexpr size_t c_eip7823InputSizeLimit = 1024;
constexpr int64_t kOsakaEmptyModExpOogGas = 21300;
constexpr int64_t kOsakaEmptyModExpOkGas = 21600;

const OpForkConfig& osakaCfg()
{
    return karstOnly().configAt(2);
}

const OpForkConfig& jovianCfg()
{
    return jovianConfig();
}

const OpForkConfig& graniteCfg()
{
    return graniteConfig();
}

std::filesystem::path osakaFixtureDir()
{
    return std::filesystem::path(__FILE__).parent_path() / "fixtures/osaka";
}

std::vector<uint8_t> loadOsakaFixture(const char* name)
{
    const auto path = osakaFixtureDir() / name;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    BOOST_REQUIRE_MESSAGE(in, std::string("missing fixture: ") + path.string());
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    BOOST_REQUIRE_MESSAGE(in, std::string("failed to read fixture: ") + path.string());
    return data;
}

std::string sha256Hex(std::span<uint8_t const> data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), hash);
    return bcos::toHex(bcos::bytesConstRef(hash, SHA256_DIGEST_LENGTH));
}

state::BlockInfo makeOsakaBlock()
{
    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;
    return block;
}

struct OsakaTxRun
{
    bcos::protocol::TransactionReceipt::Ptr receipt;
    evmone::state::StateDiff diff;
};

OsakaTxRun runOsakaOpTx(test::TestState& ts, evmc::VM& vm, const state::Transaction& tx,
    const OpForkConfig& cfg, uint64_t chainId = 1234)
{
    test::TestBlockHashes hashes;
    const auto block = makeOsakaBlock();
    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v = opValidate(ts, block, tx, {env.data(), env.size()}, cfg, fee, block.gas_limit);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    evmone::state::StateDiff diff;
    auto receipt =
        opTransition(ts, block, hashes, tx, cfg, vm, props, chainId, kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    return {std::move(receipt), std::move(diff)};
}

OsakaTxRun runOsakaContractWithCode(
    test::TestState& ts, evmc::VM& vm, evmc::bytes code, const OpForkConfig& cfg)
{
    ts[kOsakaSender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kOsakaContract] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = std::move(code)};
    seedOpPredeploys(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = kOsakaContract;
    tx.gas_limit = 5000000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    return runOsakaOpTx(ts, vm, tx, cfg);
}

int64_t osakaModExpExecutionGasUsed(
    test::TestState& ts, evmc::VM& vm, const std::vector<uint8_t>& input, const OpForkConfig& cfg)
{
    ts[kOsakaSender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    seedOpPredeploys(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = kOsakaModExp;
    tx.gas_limit = 10'000'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.data = state::bytes(input.begin(), input.end());

    test::TestBlockHashes hashes;
    const auto block = makeOsakaBlock();
    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v = opValidate(ts, block, tx, {env.data(), env.size()}, cfg, fee, block.gas_limit);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    evmone::state::State state{ts};
    ++state.get_or_insert(tx.sender).nonce;

    OpHost host{cfg.rev, vm, state, block, hashes, tx, 1234, cfg.precompiles};
    const auto outcome = runTxMessage(state, host, tx, cfg.rev, block.coinbase,
        props.props.execution_gas_limit, props.props.min_gas_cost, 0);
    BOOST_REQUIRE_EQUAL(outcome.result.status_code, EVMC_SUCCESS);
    return props.props.execution_gas_limit - outcome.result.gas_left;
}

OsakaTxRun runOsakaModExpOpTx(
    test::TestState& ts, evmc::VM& vm, const std::vector<uint8_t>& input, const OpForkConfig& cfg)
{
    ts[kOsakaSender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    seedOpPredeploys(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = kOsakaModExp;
    tx.gas_limit = 10'000'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.data = state::bytes(input.begin(), input.end());
    return runOsakaOpTx(ts, vm, tx, cfg);
}

OsakaTxRun runOsakaPrecompileOpTx(test::TestState& ts, evmc::VM& vm,
    const evmc::address& precompile, const std::vector<uint8_t>& input, const OpForkConfig& cfg,
    int64_t gasLimit = 10'000'000)
{
    ts[kOsakaSender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    seedOpPredeploys(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = precompile;
    tx.gas_limit = gasLimit;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.data = state::bytes(input.begin(), input.end());
    return runOsakaOpTx(ts, vm, tx, cfg);
}

struct OsakaHostCallResult
{
    evmc_status_code status_code;
    int64_t gas_left;
    std::vector<uint8_t> output;
};

OsakaHostCallResult osakaHostCall(evmc_revision rev, const PrecompileOverrides* overrides,
    const evmc::address& target, const std::vector<uint8_t>& input, int64_t gas)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    // Host::prepare_message(depth==0) calls get(sender); the account must exist.
    ts[kOsakaSender] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = {}};
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kOsakaSender;
    const auto block = makeOsakaBlock();
    OpHost host{rev, vm, st, block, hashes, tx, 1234, overrides};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = target;
    msg.code_address = target;
    msg.sender = kOsakaSender;
    msg.gas = gas;
    msg.input_data = input.data();
    msg.input_size = input.size();
    const auto r = host.call(msg);
    OsakaHostCallResult out{.status_code = r.status_code, .gas_left = r.gas_left, .output = {}};
    if (r.output_data != nullptr && r.output_size > 0)
        out.output.assign(r.output_data, r.output_data + r.output_size);
    return out;
}

/// The modexp ABI carries three 256-bit length headers; each stores its 64-bit value in
/// the header's last 8 bytes, big-endian.
void writeModExpLengthField(std::vector<uint8_t>& input, size_t offset, uint64_t len)
{
    for (size_t i = 0; i < 8; ++i)
        input[offset + 24 + i] = static_cast<uint8_t>((len >> (56 - i * 8)) & 0xff);
}

std::vector<uint8_t> makeOsakaEip7823ModExpInput(size_t baseLen, uint8_t baseByte)
{
    std::vector<uint8_t> input(96 + baseLen + 2, 0x00);
    writeModExpLengthField(input, 0, baseLen);
    writeModExpLengthField(input, 32, 1);
    writeModExpLengthField(input, 64, 1);
    for (size_t i = 0; i < baseLen; ++i)
        input[96 + i] = baseByte;
    input[96 + baseLen] = 0x00;
    input[96 + baseLen + 1] = 0x02;
    return input;
}

// EIP-7823 bounds each of the three length fields independently. The existing helper only
// varies base_len; this one varies any of them while keeping the exponent 0 (so the result
// is 1 mod modulus regardless of the base) and the modulus 2.
std::vector<uint8_t> makeOsakaModExpSizeInput(size_t baseLen, size_t expLen, size_t modLen)
{
    std::vector<uint8_t> input(96 + baseLen + expLen + modLen, 0x00);
    writeModExpLengthField(input, 0, baseLen);
    writeModExpLengthField(input, 32, expLen);
    writeModExpLengthField(input, 64, modLen);
    std::fill_n(input.begin() + 96, static_cast<std::ptrdiff_t>(baseLen), 0x01);
    if (modLen > 0)
    {
        // 0x00…02: modulus 2, so base^0 mod 2 == 1 and the output is a single 0x01 byte.
        input[96 + baseLen + expLen + modLen - 1] = 0x02;
    }
    return input;
}

intx::uint256 readOsakaStorageSlot(
    const test::TestState& ts, const evmc::address& addr, uint64_t slot)
{
    evmc::bytes32 key{};
    if (slot != 0)
        key.bytes[31] = static_cast<uint8_t>(slot);
    return intx::be::load<intx::uint256>(ts.get_storage(addr, key));
}

std::vector<uint8_t> makeBn256PairingInput(size_t pairs)
{
    return std::vector<uint8_t>(pairs * kBn256PairSize, 0x00);
}

// A real BN254 G2 point, loaded from the tracked oracle contract op_geth_oracle.json
// (regenerated by tools/op-geth-oracle/extract.sh from op-geth e8800cffe, vector
// bn256Pairing.json "two_point_match_3" pair-1 G2, hex offset 128..384). That vector's product
// is 1 and the precompile accepted every point in it, so this encoding is known-good; using a
// genuine point instead of a second (0, 0) means each generated pair's identity does not depend
// on how the engine treats an all-zero G2 encoding.
std::string loadBn256ValidG2PointHex()
{
    auto const path = std::filesystem::path(__FILE__).parent_path() / "op_geth_oracle.json";
    BOOST_REQUIRE_MESSAGE(std::filesystem::exists(path),
        "missing op-geth oracle at " << path.string()
                                     << " -- regenerate with tools/op-geth-oracle/extract.sh");
    std::ifstream in(path);
    BOOST_REQUIRE_MESSAGE(in.is_open(), "cannot read " << path.string());
    Json::Value root;
    BOOST_REQUIRE_MESSAGE(Json::Reader{}.parse(in, root, false), "cannot parse " << path.string());
    auto const& hex = root["bn256"]["g2_valid_point_hex"];
    BOOST_REQUIRE_MESSAGE(hex.isString(), "op_geth_oracle.json: missing bn256.g2_valid_point_hex");
    return hex.asString();
}

// `pairs` BN254 pairing pairs, each (G1 = (0, 0), the point at infinity; G2 = a valid point).
// e(infinity, Q) = 1 for any Q, so the product over every pair is 1 and a valid input must
// succeed. Size = pairs * 192B; this is the payload the Granite/Isthmus 112687-byte cap is
// asserted against.
std::vector<uint8_t> repeatInfinityG1Pairs(size_t pairs)
{
    const auto g2 = bcos::fromHex(loadBn256ValidG2PointHex());
    BOOST_REQUIRE_EQUAL(g2.size(), kBn256PairSize - 64);
    std::vector<uint8_t> input;
    input.reserve(pairs * kBn256PairSize);
    for (size_t i = 0; i < pairs; ++i)
    {
        input.insert(input.end(), 64, 0x00);
        input.insert(input.end(), g2.begin(), g2.end());
    }
    return input;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpOsakaSemanticsSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(ClzVectorsThroughKarstOpPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;

    // PUSH0 CLZ PUSH1 0 SSTORE STOP
    const auto r0 = runOsakaContractWithCode(
        ts, vm, evmc::bytes{0x5f, 0x1e, 0x60, 0x00, 0x55, 0x00}, osakaCfg());
    BOOST_REQUIRE_EQUAL(r0.receipt->status(), 0);
    BOOST_CHECK_EQUAL(readOsakaStorageSlot(ts, kOsakaContract, 0), intx::uint256{256});

    test::TestState ts1;
    const auto r1 = runOsakaContractWithCode(
        ts1, vm, evmc::bytes{0x60, 0x01, 0x1e, 0x60, 0x00, 0x55, 0x00}, osakaCfg());
    BOOST_REQUIRE_EQUAL(r1.receipt->status(), 0);
    BOOST_CHECK_EQUAL(readOsakaStorageSlot(ts1, kOsakaContract, 0), intx::uint256{255});

    test::TestState ts2;
    evmc::bytes push32Clz{0x7f, 0x80};
    for (int i = 0; i < 31; ++i)
        push32Clz.push_back(0x00);
    push32Clz.push_back(0x1e);
    push32Clz.push_back(0x60);
    push32Clz.push_back(0x00);
    push32Clz.push_back(0x55);
    push32Clz.push_back(0x00);
    const auto r2 = runOsakaContractWithCode(ts2, vm, std::move(push32Clz), osakaCfg());
    BOOST_REQUIRE_EQUAL(r2.receipt->status(), 0);
    BOOST_CHECK_EQUAL(readOsakaStorageSlot(ts2, kOsakaContract, 0), intx::uint256{0});
}

// clang-format off
BOOST_AUTO_TEST_CASE(ClzUndefinedBeforeOsakaOnJovianPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    const auto r = runOsakaContractWithCode(ts, vm, evmc::bytes{0x5f, 0x1e, 0x00}, jovianConfig());
    BOOST_CHECK_NE(r.receipt->status(), 0);
}

// clang-format off
BOOST_AUTO_TEST_CASE(ClzCostsFiveGasUnderOsaka, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState push0Ts;
    const auto push0Run =
        runOsakaContractWithCode(push0Ts, vm, evmc::bytes{0x5f, 0x00}, osakaCfg());
    BOOST_REQUIRE_EQUAL(push0Run.receipt->status(), 0);

    test::TestState clzTs;
    const auto clzRun =
        runOsakaContractWithCode(clzTs, vm, evmc::bytes{0x5f, 0x1e, 0x00}, osakaCfg());
    BOOST_REQUIRE_EQUAL(clzRun.receipt->status(), 0);

    const auto push0Gas = static_cast<int64_t>(push0Run.receipt->gasUsed());
    const auto clzGas = static_cast<int64_t>(clzRun.receipt->gasUsed());
    BOOST_CHECK_EQUAL(clzGas - push0Gas, 5);
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7823ModExpLengthBoundsThroughKarstOpPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    const auto ok = runOsakaModExpOpTx(
        ts, vm, makeOsakaEip7823ModExpInput(c_eip7823InputSizeLimit, 0x01), osakaCfg());
    BOOST_REQUIRE_EQUAL(ok.receipt->status(), 0);
    const auto output = ok.receipt->output();
    BOOST_REQUIRE_EQUAL(output.size(), 1U);
    BOOST_CHECK_EQUAL(output[0], 0x01);

    test::TestState tsFail;
    const auto fail = runOsakaModExpOpTx(
        tsFail, vm, makeOsakaEip7823ModExpInput(c_eip7823InputSizeLimit + 1, 0x01), osakaCfg());
    BOOST_CHECK_NE(fail.receipt->status(), 0);
    BOOST_CHECK_EQUAL(static_cast<int64_t>(fail.receipt->gasUsed()), 10'000'000);
}

/// Each length field is bounded independently at 1024 (EIP-7823; op-revm enforces all three
/// in modexp.rs). 1024 is the last legal value, 1025 the first illegal one; the illegal case
/// must burn the whole gas limit exactly like the base_len case already pinned.
static void checkModExpSizeBound(size_t baseLen, size_t expLen, size_t modLen, bool legal)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    const auto run =
        runOsakaModExpOpTx(ts, vm, makeOsakaModExpSizeInput(baseLen, expLen, modLen), osakaCfg());
    if (legal)
    {
        BOOST_REQUIRE_EQUAL(run.receipt->status(), 0);
        const auto output = run.receipt->output();
        // EIP-2565: the modexp output is exactly modulus-length bytes, big-endian
        // zero-padded. base^0 mod 2 == 1, so the payload is (modLen-1) zero bytes
        // followed by 0x01 — a single 0x01 byte only when modLen == 1.
        BOOST_REQUIRE_EQUAL(output.size(), modLen);
        for (size_t i = 0; i + 1 < output.size(); ++i)
        {
            BOOST_CHECK_EQUAL(output[i], 0x00);
        }
        BOOST_CHECK_EQUAL(output[output.size() - 1], 0x01);
    }
    else
    {
        BOOST_CHECK_NE(run.receipt->status(), 0);
        BOOST_CHECK_EQUAL(static_cast<int64_t>(run.receipt->gasUsed()), 10'000'000);
    }
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7823ModExpBaseLenBoundThroughKarstOpPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    checkModExpSizeBound(c_eip7823InputSizeLimit, 1, 1, true);
    checkModExpSizeBound(c_eip7823InputSizeLimit + 1, 1, 1, false);
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7823ModExpExpLenBoundThroughKarstOpPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    checkModExpSizeBound(1, c_eip7823InputSizeLimit, 1, true);
    checkModExpSizeBound(1, c_eip7823InputSizeLimit + 1, 1, false);
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7823ModExpModLenBoundThroughKarstOpPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    checkModExpSizeBound(1, 1, c_eip7823InputSizeLimit, true);
    checkModExpSizeBound(1, 1, c_eip7823InputSizeLimit + 1, false);
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7883EmptyModExpFloorGasThroughKarstOpPath, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState oogTs;
    const auto oog =
        runOsakaPrecompileOpTx(oogTs, vm, kOsakaModExp, {}, osakaCfg(), kOsakaEmptyModExpOogGas);
    BOOST_CHECK_NE(oog.receipt->status(), 0);

    test::TestState okTs;
    const auto ok =
        runOsakaPrecompileOpTx(okTs, vm, kOsakaModExp, {}, osakaCfg(), kOsakaEmptyModExpOkGas);
    BOOST_CHECK_EQUAL(ok.receipt->status(), 0);
}

// clang-format off
BOOST_AUTO_TEST_CASE(OsakaFixtureManifestSha256Matches, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    struct Item
    {
        char const* file;
        char const* sha256;
    };
    constexpr Item items[] = {
        {"p256verify_valid_input.bin",
            "97fb89c23bb467103846ad24b0405dc263a9745b9da3d20353a37cb106cb2f65"},
        {"modexp_nagydani_1_square.input.bin",
            "0138e3fb9e62139ad1d9fff645b9df84aae86e4745bfbb993f95af5a79f0307f"},
        {"modexp_nagydani_1_square.expected.bin",
            "173bc056487155483fe9eb19dd4a5af452d6d29787349b91c7b1dc8ec129b5c4"},
        {"modexp_nagydani_2_pow0x10001.input.bin",
            "2659dd123f53088594b9cbbaaf3ac4f3b42b557e60b42991da5162d02de7192a"},
        {"modexp_nagydani_2_pow0x10001.expected.bin",
            "e2fc81c754623d0c4f08842b0ae835bf06c3442394f513168c576d2efbd5f275"},
    };
    for (auto const& item : items)
    {
        auto const data = loadOsakaFixture(item.file);
        BOOST_CHECK_EQUAL(sha256Hex(data), item.sha256);
    }
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7883ModExpNagydani1GasFromFixture, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    const auto input = loadOsakaFixture("modexp_nagydani_1_square.input.bin");
    test::TestState tsPrague;
    test::TestState tsOsaka;
    const auto pragueExec = osakaModExpExecutionGasUsed(tsPrague, vm, input, jovianConfig());
    const auto osakaExec = osakaModExpExecutionGasUsed(tsOsaka, vm, input, osakaCfg());
    BOOST_CHECK_EQUAL(osakaExec - pragueExec, 500 - 200);

    const auto expected = loadOsakaFixture("modexp_nagydani_1_square.expected.bin");
    const auto out =
        osakaHostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kOsakaModExp, input, 10'000'000);
    BOOST_REQUIRE_EQUAL(out.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        out.output.begin(), out.output.end(), expected.begin(), expected.end());
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7883ModExpNagydani2PowGasFromFixture, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    const auto input = loadOsakaFixture("modexp_nagydani_2_pow0x10001.input.bin");
    test::TestState tsPrague;
    test::TestState tsOsaka;
    const auto pragueExec = osakaModExpExecutionGasUsed(tsPrague, vm, input, jovianConfig());
    const auto osakaExec = osakaModExpExecutionGasUsed(tsOsaka, vm, input, osakaCfg());
    BOOST_CHECK_EQUAL(osakaExec - pragueExec, 8192 - 1365);

    const auto expected = loadOsakaFixture("modexp_nagydani_2_pow0x10001.expected.bin");
    const auto out =
        osakaHostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kOsakaModExp, input, 10'000'000);
    BOOST_REQUIRE_EQUAL(out.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        out.output.begin(), out.output.end(), expected.begin(), expected.end());
}

// clang-format off
BOOST_AUTO_TEST_CASE(Eip7951P256VerifyFromFixture, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    const auto input = loadOsakaFixture("p256verify_valid_input.bin");
    std::vector<uint8_t> expectedSuccess(32, 0x00);
    expectedSuccess[31] = 0x01;

    test::TestState jovTs;
    const auto jovianOk = runOsakaPrecompileOpTx(jovTs, vm, kOsakaP256, input, jovianConfig());
    BOOST_CHECK_EQUAL(jovianOk.receipt->status(), 0);
    const auto jovOut = jovianOk.receipt->output();
    BOOST_REQUIRE_EQUAL(jovOut.size(), 32U);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        jovOut.begin(), jovOut.end(), expectedSuccess.begin(), expectedSuccess.end());

    test::TestState karstTs;
    const auto karstOk = runOsakaPrecompileOpTx(karstTs, vm, kOsakaP256, input, osakaCfg());
    BOOST_CHECK_EQUAL(karstOk.receipt->status(), 0);
    const auto karstOut = karstOk.receipt->output();
    BOOST_REQUIRE_EQUAL(karstOut.size(), 32U);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        karstOut.begin(), karstOut.end(), expectedSuccess.begin(), expectedSuccess.end());

    const auto jovianFail =
        osakaHostCall(EVMC_PRAGUE, &jovianPrecompileOverrides(), kOsakaP256, input, 3449);
    BOOST_CHECK_EQUAL(jovianFail.status_code, EVMC_OUT_OF_GAS);

    const auto karstFail =
        osakaHostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kOsakaP256, input, 6899);
    BOOST_CHECK_EQUAL(karstFail.status_code, EVMC_OUT_OF_GAS);
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstOrdinaryTxRejectsGasOverEip7825Cap, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    test::TestState ts;
    ts[kOsakaSender] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT + 1;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto r = opValidate(
        ts, makeOsakaBlock(), tx, {env.data(), env.size()}, osakaCfg(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(r));
    BOOST_CHECK(std::get<std::error_code>(r) ==
                evmone::state::make_error_code(evmone::state::MAX_GAS_LIMIT_EXCEEDED));
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstEthCallSkipsEip7825MaxGasLimit, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // geth #32641: eth_call skips the Osaka 2^24 cap. Block admission (default
    // policy) still rejects — see KarstOrdinaryTxRejectsGasOverEip7825Cap.
    test::TestState ts;
    ts[kOsakaSender] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT + 1;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto r = opValidate(ts, makeOsakaBlock(), tx, {env.data(), env.size()}, osakaCfg(),
        OpFeeParams{}, 30000000, evmone::state::TxValidationPolicy{.enforce_max_tx_gas = false});
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(r));
}

// The exemption pinned here matches the second OP implementation, op-revm: its validate_env
// override returns Ok() for DEPOSIT_TRANSACTION_TYPE before reaching the baseline check that
// enforces revm's TxGasLimitCap (op-revm/src/handler.rs:81-100 vs
// revm-handler-*/src/validation.rs:150-159) — so deposits are exempt there too, while op-geth
// applies the cap to deposits (core/state_transition.go:379-383, excluded from its
// failed-deposit tolerance at :489-491). The specs are silent, so the honest label is
// "matches op-revm; op-geth differs" — NOT "spec-aligned". An earlier revision of this comment
// (and of DIVERGENCES.md) cited "specs/protocol/karst/overview.md:20"; that line does not
// exist (the file is a 464-byte / 16-line stub at pin 564a0ce) and the citation was removed.
// See da-matrix/DIVERGENCES.md `eip7825_deposit_exemption` (WI-35, closed 2026-09-13).
// clang-format off
BOOST_AUTO_TEST_CASE(DepositExemptFromEip7825MaxGasLimit, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kOsakaSender] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;
    const int64_t overCap = static_cast<int64_t>(evmone::state::MAX_TX_GAS_LIMIT + 1);
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kOsakaSender,
        .to = kOsakaSender,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = overCap,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, makeOsakaBlock(), hashes, dep, osakaCfg(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);

    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(static_cast<int64_t>(static_cast<uint64_t>(r->gasUsed())), 21000);
    BOOST_CHECK_EQUAL(ts.at(kOsakaSender).balance, intx::uint256{100});
    BOOST_CHECK_EQUAL(ts.at(kOsakaSender).nonce, 1u);
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstBn256PairingCapsAt300Pairs, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // All-zero G1/G2 is the BN254 point at infinity; e(∞, ∞) = 1 (same fixture as
    // OpHostTest::JovianBn256PairingInputAtLimitExecutes). Gas = 45000 + n*34000.
    const auto* jovian08 = jovianPrecompileOverrides().find(kOsakaBn256Pairing);
    BOOST_REQUIRE((jovian08) != nullptr);
    BOOST_CHECK_EQUAL(jovian08->max_input_size, 427 * kBn256PairSize);

    constexpr int64_t kPairingGas = 15'000'000;
    const auto ok300 = osakaHostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kOsakaBn256Pairing,
        makeBn256PairingInput(300), kPairingGas);
    BOOST_CHECK_EQUAL(ok300.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(ok300.gas_left, 0);
    BOOST_REQUIRE_EQUAL(ok300.output.size(), 32U);
    BOOST_CHECK_EQUAL(ok300.output[31], 0x01);

    const auto halt301 = osakaHostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kOsakaBn256Pairing,
        makeBn256PairingInput(301), kPairingGas);
    BOOST_CHECK_EQUAL(halt301.status_code, EVMC_FAILURE);
    BOOST_CHECK_EQUAL(halt301.gas_left, 0);

    const auto empty =
        osakaHostCall(EVMC_OSAKA, &karstPrecompileOverrides(), kOsakaBn256Pairing, {}, kPairingGas);
    BOOST_CHECK_EQUAL(empty.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(empty.output.size(), 32U);
    BOOST_CHECK_EQUAL(empty.output[31], 0x01);
}

// The cap compares with '>': exactly MAX_TX_GAS_LIMIT is legal. The existing case pins
// limit+1 only, so the legal side of the boundary is the untested half.
// Constant three-way agreement (Task 0 ledger S5): this repo's MAX_TX_GAS_LIMIT (2**24)
// == op-geth params.MaxTxGas == op-revm eip7825::TX_GAS_LIMIT_CAP (all at pinned SHAs).
// clang-format off
BOOST_AUTO_TEST_CASE(OrdinaryTxAtExactlyEip7825CapIsAccepted, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    test::TestState ts;
    ts[kOsakaSender] = {.nonce = 0, .balance = intx::uint256{1} << 60, .storage = {}, .code = {}};
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto r = opValidate(
        ts, makeOsakaBlock(), tx, {env.data(), env.size()}, osakaCfg(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(r));
}

// The EIP-7825 cap is Osaka-gated (state.cpp:385 'rev >= EVMC_OSAKA'): before Osaka the
// same over-cap tx must not be rejected BY THAT RULE. The assertion pins the rule's
// non-applicability, not "the whole validation passes" -- other rejection reasons must
// not make this cell red (and must not be misread as an implementation defect).
// clang-format off
BOOST_AUTO_TEST_CASE(OverCapTxIsNotRejectedByEip7825BeforeOsaka, * boost::unit_test::label("fork-jovian"))
// clang-format on
{
    test::TestState ts;
    ts[kOsakaSender] = {.nonce = 0, .balance = intx::uint256{1} << 60, .storage = {}, .code = {}};
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kOsakaSender;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.gas_limit = evmone::state::MAX_TX_GAS_LIMIT + 1;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto r = opValidate(
        ts, makeOsakaBlock(), tx, {env.data(), env.size()}, jovianCfg(), OpFeeParams{}, 30000000);
    // Executes on both paths: an honest pre-Osaka result either holds OpTxProperties or
    // fails for some reason OTHER than the Osaka-gated cap. One assertion, always run —
    // a zero-assertion pass cannot be told apart from a skipped case in the logs.
    const bool rejectedByEip7825 =
        std::holds_alternative<std::error_code>(r) &&
        std::get<std::error_code>(r) ==
            evmone::state::make_error_code(evmone::state::MAX_GAS_LIMIT_EXCEEDED);
    BOOST_CHECK(!rejectedByEip7825);
}

// 586 pairs = 112512B <= 112687 (Granite/Isthmus cap): the precompile must run and succeed.
// Each pair is (G1 = point at infinity, G2 = valid point) so every pair's pairing is 1.
// Labels: granite/holocene/isthmus only — Jovian's cap is 81984 and Karst's is 57600, where
// a 586-pair input is correctly rejected (Task 0 F-E2-1).
// clang-format off
BOOST_AUTO_TEST_CASE(GraniteBn256PairingAt586PairsSucceeds, * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    auto input = repeatInfinityG1Pairs(586);
    BOOST_REQUIRE_EQUAL(input.size(), 586 * kBn256PairSize);
    // 45_000 + 34_000 * 586 ≈ 19.97M gas, so the default 10M helper cap is too small.
    auto run = runOsakaPrecompileOpTx(ts, vm, kOsakaBn256Pairing, input, graniteCfg(), 30'000'000);
    BOOST_CHECK_EQUAL(run.receipt->status(), 0);
}

// 587 pairs = 112704B > 112687: rejected by the Granite size cap (specs granite/exec-engine.md).
// Assertion is rejection only; whether the size-check halt is distinguishable from a
// pairing-math failure is recorded as residual in the Task 0 ledger (F-S4-1).
// clang-format off
BOOST_AUTO_TEST_CASE(GraniteBn256PairingAt587PairsIsRejected, * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    auto input = repeatInfinityG1Pairs(587);
    BOOST_REQUIRE_EQUAL(input.size(), 587 * kBn256PairSize);
    auto run = runOsakaPrecompileOpTx(ts, vm, kOsakaBn256Pairing, input, graniteCfg(), 30'000'000);
    BOOST_CHECK_NE(run.receipt->status(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
