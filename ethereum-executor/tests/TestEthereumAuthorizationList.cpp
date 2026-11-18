/// @file TestEthereumAuthorizationList.cpp
/// @brief EIP-7702 cross-chain authorization rejection, pinned against
///        eth_transition_detail::processAuthorizationList (EthereumTransition.h).
///
/// Ported from the retired bcos-evm/test/eth/EthTransitionTest.cpp's
/// AuthorizationForAnotherChainIsSkipped (step 4.2): step 1 of the EIP-7702
/// authorization processing must compare each authorization's chain id against
/// the NODE's chain id. validate_transaction never checks tx.chain_id, so if the
/// tx-carried value were trusted instead, the comparison would be
/// "user input == user input" — an authorization signed for chain X would write
/// a delegation on chain Y by simply setting tx.chain_id = X. The golden
/// signature below is signed for chain_id = 1; on a node running chain 999 the
/// delegation must NOT be written.

#include <boost/test/unit_test.hpp>

#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "ethereum-executor/EthereumState.h"
#include "ethereum-executor/EthereumTransition.h"
#include "ethereum-executor/tests/TestMemoryStorage.h"

#include <evmone/evmone.h>
#include <algorithm>
#include <evmc/evmc.hpp>
#include <evmone/vm.hpp>

using namespace bcos;
using namespace bcos::executor_v1;
using namespace evmc::literals;

namespace
{
// EIP-7702 golden values: signed with eth-account per
// keccak256(0x05 || rlp([chain_id, address, nonce])).
// Private key 0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d,
// signature covers chain_id = 1, addr = kDelegate, nonce = 0. Do not hand-edit.
constexpr auto kAuthority = 0x70997970C51812dc3A010C7d01b50e0d17dc79C8_address;
constexpr auto kDelegate = 0x00000000000000000000000000000000000000cc_address;
constexpr std::string_view kAuthR =
    "0x8bd0c047683d78ac6855fd9997e17dd64c4941334308c2708930682e1831c42a";
constexpr std::string_view kAuthS =
    "0x7399ba8d6bdec8bacec1cfb93d1f1bd00bedbade84959bda53464acaaa32f330";

/// The delegation designator prefix (EIP-7702): 0xef0100 || address.
bool isDelegationDesignator(evmc::bytes_view code, evmc::address const& expected) noexcept
{
    return code.size() == 23 && code[0] == 0xef && code[1] == 0x01 && code[2] == 0x00 &&
           std::equal(code.begin() + 3, code.end(), std::begin(expected.bytes));
}

/// A Web3 set-code transaction carrying one authorization mirror signed for chain 1.
/// Only authorizationList() is read by processAuthorizationList; the rest of the
/// transaction is inert for this test.
bcostars::protocol::TransactionImpl makeAuthTx()
{
    bcostars::protocol::TransactionImpl tx;
    auto& inner = tx.mutableInner();
    inner.type = static_cast<tars::Char>(1);             // TransactionType::Web3Transaction
    inner.web3TypedTxKind = static_cast<tars::Char>(4);  // EIP-7702 set-code

    bcostars::AuthorizationEntry entry;
    entry.chainID = 1;  // signed for chain 1 — the attacker's claim
    entry.address = "00000000000000000000000000000000000000cc";
    entry.nonce = 0;
    entry.r = std::string{kAuthR};
    entry.s = std::string{kAuthS};
    entry.v = 0;
    inner.data.authorizationList.emplace_back(std::move(entry));
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthereumAuthorizationListTest)

BOOST_AUTO_TEST_CASE(AuthorizationForAnotherChainIsSkipped)
{
    // Fills `state` by processing the golden authorization under `nodeChainId`.
    const auto runOnChain = [](eth::EthereumState<MutableStorage>& state, uint64_t nodeChainId) {
        const auto tx = makeAuthTx();
        eth::eth_transition_detail::processAuthorizationList(state, nodeChainId, tx);
    };

    // Positive control: on chain 1 the authorization holds, the delegation
    // designator is written and the authority's nonce advances (EIP-7702 step 11).
    // Without this arm the negative assertions below could pass for an unrelated
    // reason (e.g. ecrecover failing outright).
    {
        MutableStorage storage;
        eth::EthereumState state{storage};
        runOnChain(state, 1);
        const auto code = state.get_code(kAuthority);
        BOOST_REQUIRE_MESSAGE(isDelegationDesignator(code, kDelegate),
            "chain 1: delegation designator (0xef0100||addr) must be written");
        BOOST_CHECK_EQUAL(state.get(kAuthority).nonce, 1u);
    }

    // Discriminating assertion: the node runs chain 999, the authorization was
    // only ever signed for chain 1 — it must be skipped entirely: the authority
    // account is not even created (step 1 continues before get_or_insert).
    {
        MutableStorage storage;
        eth::EthereumState state{storage};
        runOnChain(state, 999);
        BOOST_CHECK_MESSAGE(state.find(kAuthority) == nullptr,
            "cross-chain authorization must not touch the authority account");
        BOOST_CHECK_MESSAGE(state.get_code(kAuthority).empty(),
            "cross-chain authorization must NOT write a delegation designator");
        for (auto const& [addr, account] : state.modified())
        {
            BOOST_CHECK_MESSAGE(
                account.code.empty() || !isDelegationDesignator(account.code, kDelegate),
                "no account may receive a delegation designator on a foreign chain");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

namespace
{
struct TraceFixture
{
    MutableStorage storage;
    eth::EthereumState<MutableStorage> state{storage};
    evmc::VM vm{evmc_create_evmone()};
    eth::EthBlockInfo block{};
    eth::EthL1Policy policy;
    eth::EthereumCallTracer* tracer;
    static constexpr auto sender = 0xaa_address;
    static constexpr auto target = 0xbb_address;
    TraceFixture(bool onlyTop = false, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        auto owned =
            std::make_unique<eth::EthereumCallTracer>(scheduler::TraceOptions{timeout, onlyTop});
        tracer = owned.get();
        tracer->setRecording(true);
        static_cast<evmone::VM*>(vm.get_raw_pointer())->add_tracer(std::move(owned));
        state.get_or_insert(sender).nonce = 1;
        block.gas_limit = 30000000;
    }
    void code(evmc::address address, std::initializer_list<uint8_t> bytes)
    {
        auto& account = state.get_or_insert(address);
        account.code = {bytes.begin(), bytes.size()};
        account.code_hash = eth::evm::keccak256(account.code);
    }
    evmc::Result call(
        int64_t gas = 1000000, bool create = false, evmc::bytes_view input = {}, int depth = 0)
    {
        eth::EthereumHost<MutableStorage> host{
            EVMC_CANCUN, vm, state, block, {}, std::nullopt, {}, 1, policy, tracer};
        evmc_message msg{};
        msg.kind = create ? EVMC_CREATE : EVMC_CALL;
        msg.sender = sender;
        if (!create)
            msg.recipient = msg.code_address = target;
        msg.depth = depth;
        msg.gas = gas;
        msg.input_data = input.data();
        msg.input_size = input.size();
        return host.call(msg);
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(EthereumCallTracerTest)
BOOST_AUTO_TEST_CASE(ChildRevertPreservesSuccessfulParentAndOnlyTopCall)
{
    for (bool onlyTop : {false, true})
    {
        TraceFixture f(onlyTop);
        f.code(TraceFixture::target,
            {0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0xcc, 0x61, 0xff, 0xff, 0xf1, 0});
        f.code(0xcc_address, {0x60, 0, 0x60, 0, 0xfd});
        auto result = f.call();
        f.tracer->check();
        BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
        auto root = f.tracer->result();
        BOOST_REQUIRE(root);
        BOOST_CHECK_EQUAL(root->type, "CALL");
        BOOST_CHECK(root->error.empty());
        BOOST_REQUIRE_EQUAL(root->calls.size(), onlyTop ? 0u : 1u);
        if (!onlyTop)
            BOOST_CHECK_EQUAL(root->calls.front()->error, "execution reverted");
    }
}
BOOST_AUTO_TEST_CASE(CallWithinStaticCallKeepsActualOpcodeType)
{
    TraceFixture f;
    f.code(TraceFixture::target,
        {0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0xcc, 0x61, 0xff, 0xff, 0xfa, 0});
    f.code(0xcc_address,
        {0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0xdd, 0x61, 0xff, 0xff, 0xf1, 0});
    f.code(0xdd_address, {0});
    auto result = f.call();
    f.tracer->check();
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    auto root = f.tracer->result();
    BOOST_REQUIRE_EQUAL(root->calls.size(), 1u);
    auto child = root->calls.front();
    BOOST_CHECK_EQUAL(child->type, "STATICCALL");
    BOOST_REQUIRE_EQUAL(child->calls.size(), 1u);
    BOOST_CHECK_EQUAL(child->calls.front()->type, "CALL");
}
BOOST_AUTO_TEST_CASE(CreatePreservesRuntimeOutput)
{
    TraceFixture f;
    const uint8_t init[] = {0x60, 1, 0x60, 0, 0xf3};
    auto result = f.call(1000000, true, {init, sizeof(init)});
    f.tracer->check();
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(f.tracer->result()->output, "0x00");
    BOOST_CHECK(!f.tracer->result()->to.empty());
}
BOOST_AUTO_TEST_CASE(CreateEmptyRuntimeOmitsOutput)
{
    TraceFixture f;
    const uint8_t init[] = {0x00};
    auto result = f.call(1000000, true, {init, sizeof(init)});
    f.tracer->check();
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK(f.tracer->result()->output.empty());
}
BOOST_AUTO_TEST_CASE(InsufficientBalanceChildIsRecorded)
{
    TraceFixture f;
    f.code(TraceFixture::target,
        {0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 1, 0x60, 0xcc, 0x61, 0xff, 0xff, 0xf1, 0});
    auto result = f.call();
    f.tracer->check();
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    auto root = f.tracer->result();
    BOOST_REQUIRE_EQUAL(root->calls.size(), 1u);
    BOOST_CHECK_EQUAL(root->calls.front()->error, "insufficient balance for transfer");
    BOOST_CHECK_EQUAL(root->calls.front()->gasUsed, "0x0");
}
BOOST_AUTO_TEST_CASE(PrecompileFailureRetainsValidatorReason)
{
    TraceFixture f;
    f.code(TraceFixture::target,
        {0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 9, 0x61, 0xff, 0xff, 0xf1, 0});
    auto result = f.call();
    f.tracer->check();
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    auto root = f.tracer->result();
    BOOST_REQUIRE_EQUAL(root->calls.size(), 1u);
    BOOST_CHECK_EQUAL(root->calls.front()->error, "invalid input length");
}
BOOST_AUTO_TEST_CASE(MalformedBlsInputChecksGasBeforeErrorReason)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.code_address = 0x0f_address;
    std::string_view reason;
    message.gas = 37699;
    auto underfunded = eth::evm::call_precompile(EVMC_PRAGUE, message, &reason);
    BOOST_CHECK_EQUAL(underfunded.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK(reason.empty());
    message.gas = 37700;
    auto malformed = eth::evm::call_precompile(EVMC_PRAGUE, message, &reason);
    BOOST_CHECK_EQUAL(malformed.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(reason, "invalid input length");
}
BOOST_AUTO_TEST_CASE(DepthFailureTakesPrecedenceOverInsufficientBalance)
{
    TraceFixture f;
    f.code(TraceFixture::target,
        {0x60, 0, 0x60, 0, 0x60, 0, 0x60, 0, 0x60, 1, 0x60, 0xcc, 0x61, 0xff, 0xff, 0xf1, 0});
    auto result = f.call(1000000, false, {}, 1024);
    f.tracer->check();
    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    auto root = f.tracer->result();
    BOOST_REQUIRE_EQUAL(root->calls.size(), 1u);
    BOOST_CHECK_EQUAL(root->calls.front()->error, "max call depth exceeded");
}
BOOST_AUTO_TEST_CASE(CryptoValidatorsExposeNativeFailureReason)
{
    const auto invoke = [](uint8_t address, bcos::bytes input) {
        evmc_message message{};
        message.kind = EVMC_CALL;
        message.code_address.bytes[19] = address;
        message.input_data = input.data();
        message.input_size = input.size();
        message.gas = 1000000;
        std::string_view error;
        auto result = eth::evm::call_precompile(EVMC_PRAGUE, message, &error);
        BOOST_CHECK_NE(result.status_code, EVMC_SUCCESS);
        return std::string(error);
    };
    BOOST_CHECK_EQUAL(invoke(10, bcos::bytes(192, 0)), "mismatched versioned hash");
    auto field = bcos::bytes(64, 0xff);
    BOOST_CHECK_EQUAL(invoke(6, field), "invalid fp.Element encoding");
    auto curve = bcos::bytes(64, 0);
    curve[31] = curve[63] = 1;
    BOOST_CHECK_EQUAL(invoke(6, curve), "point is not on curve");
    auto bls = bcos::bytes(256, 0);
    bls[63] = bls[127] = 1;
    BOOST_CHECK_EQUAL(invoke(11, bls), "invalid point: not on curve");
    bls[0] = 1;
    BOOST_CHECK_EQUAL(invoke(11, bls), "invalid field element top bytes");
}
BOOST_AUTO_TEST_CASE(ModexpLengthRejectionChecksNominalGasFirst)
{
    bcos::bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    input[63] = 1;
    input[95] = 1;
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.code_address = 0x05_address;
    message.input_data = input.data();
    message.input_size = input.size();
    std::string_view error;
    message.gas = 33281;
    auto low = eth::evm::call_precompile(EVMC_OSAKA, message, &error);
    BOOST_CHECK_EQUAL(low.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK(error.empty());
    message.gas = 33282;
    auto sufficient = eth::evm::call_precompile(EVMC_OSAKA, message, &error);
    BOOST_CHECK_EQUAL(sufficient.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(error, "one or more of base/exponent/modulus length exceeded 1024 bytes");
    input[31] = 0;
    message.gas = 1000000;
    auto valid = eth::evm::call_precompile(EVMC_OSAKA, message, &error);
    BOOST_CHECK_EQUAL(valid.status_code, EVMC_SUCCESS);
    BOOST_CHECK(error.empty());
}
BOOST_AUTO_TEST_CASE(TimeoutInterruptsLoopWithoutHostCalls)
{
    TraceFixture f(false, std::chrono::milliseconds(1));
    f.code(TraceFixture::target, {0x5b, 0x60, 0, 0x56});
    auto started = std::chrono::steady_clock::now();
    auto result = f.call(std::numeric_limits<int64_t>::max());
    BOOST_CHECK_NE(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EXCEPTION(f.tracer->check(), std::runtime_error,
        [](auto const& error) { return std::string_view(error.what()) == "execution timeout"; });
    BOOST_CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
}
BOOST_AUTO_TEST_CASE(OutputAndFrameBudgetsFailClosed)
{
    eth::EthereumCallTracer tracer({});
    tracer.setRecording(true);
    evmc_message msg{};
    msg.input_size = eth::EthereumCallTracer::maxBytes;
    tracer.enter(msg);
    BOOST_CHECK_THROW(tracer.check(), std::runtime_error);
    eth::EthereumCallTracer frames({});
    frames.setRecording(true);
    msg.input_size = 0;
    frames.enter(msg);
    evmc_result success{};
    for (size_t i = 0; i < eth::EthereumCallTracer::maxFrames; ++i)
    {
        frames.enter(msg);
        frames.exit(msg, success);
    }
    BOOST_CHECK_THROW(frames.check(), std::runtime_error);
}
BOOST_AUTO_TEST_SUITE_END()
