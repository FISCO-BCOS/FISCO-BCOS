/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared bytecode and fixture helpers for EIP-2929 transaction-executor tests.
 */

#pragma once

#include "../bcos-transaction-executor/vm/HostContext.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/FixedBytes.h"
#include <evmc/evmc.h>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::test::eip2929
{

/// PUSH20 <addr> EXTCODESIZE — warms @p target in current EVM frame.
inline bcos::bytes warmAccountExtCodeSizeBytecode(evmc_address const& target)
{
    bcos::bytes code;
    code.reserve(23);
    code.push_back(0x73);  // PUSH20
    code.insert(code.end(), target.bytes, target.bytes + sizeof(target.bytes));
    code.push_back(0x3b);  // EXTCODESIZE
    return code;
}

/// PUSH20 <addr> EXTCODESIZE PUSH20 <addr> EXTCODESIZE STOP — double probe for gas measurement.
inline bcos::bytes doubleExtCodeSizeBytecode(evmc_address const& target)
{
    auto code = warmAccountExtCodeSizeBytecode(target);
    code.push_back(0x73);  // PUSH20
    code.insert(code.end(), target.bytes, target.bytes + sizeof(target.bytes));
    code.push_back(0x3b);  // EXTCODESIZE
    code.push_back(0x00);  // STOP
    return code;
}

/// PUSH20 <addr> EXTCODESIZE PUSH0 PUSH0 REVERT
inline bcos::bytes warmAccountThenRevertBytecode(evmc_address const& target)
{
    auto code = warmAccountExtCodeSizeBytecode(target);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0xfd);
    return code;
}

/// PUSH20 <addr> EXTCODESIZE STOP
inline bcos::bytes warmAccountThenStopBytecode(evmc_address const& target)
{
    auto code = warmAccountExtCodeSizeBytecode(target);
    code.push_back(0x00);  // STOP
    return code;
}

/// Two cold EXTCODESIZE probes — exceeds a small child gas stipend (EIP-2929 cold cost).
inline bcos::bytes warmTwoAccountsExtCodeSizeBytecode(
    evmc_address const& target1, evmc_address const& target2)
{
    auto code = warmAccountExtCodeSizeBytecode(target1);
    code.push_back(0x73);
    code.insert(code.end(), target2.bytes, target2.bytes + sizeof(target2.bytes));
    code.push_back(0x3b);
    return code;
}

namespace detail
{
inline void appendCallGas(bcos::bytes& code, int64_t callGas)
{
    if (callGas <= 0xFFFF)
    {
        code.push_back(0x61);
        code.push_back(static_cast<uint8_t>((callGas >> 8) & 0xFF));
        code.push_back(static_cast<uint8_t>(callGas & 0xFF));
    }
    else
    {
        code.push_back(0x62);
        code.push_back(static_cast<uint8_t>((callGas >> 16) & 0xFF));
        code.push_back(static_cast<uint8_t>((callGas >> 8) & 0xFF));
        code.push_back(static_cast<uint8_t>(callGas & 0xFF));
    }
}

/// evmone call_impl pops (top→bottom): gas, to, [value,] argsOffset, argsSize, retOffset, retSize.
inline void appendCallLikeOpcode(
    bcos::bytes& code, evmc_address const& callee, int64_t callGas, uint8_t opcode)
{
    for (int i = 0; i < 4; ++i)
    {
        code.push_back(0x60);
        code.push_back(0x00);
    }
    if (opcode == 0xf1)
    {
        code.push_back(0x60);
        code.push_back(0x00);
    }
    code.push_back(0x73);
    code.insert(code.end(), callee.bytes, callee.bytes + 20);
    appendCallGas(code, callGas);
    code.push_back(opcode);
}

inline void appendCallOpcode(bcos::bytes& code, evmc_address const& callee, int64_t callGas)
{
    appendCallLikeOpcode(code, callee, callGas, 0xf1);
}
}  // namespace detail

/// Outer: CALL child, then REVERT. @p inner must already hold code.
/// Uses a fixed call gas stipend so the outer frame retains gas for REVERT.
inline bcos::bytes callThenRevertBytecode(evmc_address const& inner, int64_t callGas = 500'000)
{
    bcos::bytes code;
    detail::appendCallOpcode(code, inner, callGas);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0xfd);  // REVERT
    return code;
}

/// Outer: STATICCALL (0xfa) child with EVMC_STATIC, then REVERT.
inline bcos::bytes staticCallThenRevertBytecode(
    evmc_address const& inner, int64_t callGas = 500'000)
{
    bcos::bytes code;
    detail::appendCallLikeOpcode(code, inner, callGas, 0xfa);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0xfd);  // REVERT
    return code;
}

/// Outer: DELEGATECALL (0xf4) callee in parent storage context, then STOP.
inline bcos::bytes delegateCallThenStopBytecode(
    evmc_address const& callee, int64_t callGas = 500'000)
{
    bcos::bytes code;
    detail::appendCallLikeOpcode(code, callee, callGas, 0xf4);
    code.push_back(0x00);  // STOP
    return code;
}

/// PUSH0 PUSH0 SSTORE STOP — touches storage slot 0 (for warm-set rollback tests).
inline bcos::bytes storageWriterBytecode()
{
    return {0x60, 0x00, 0x60, 0x00, 0x55, 0x00};
}

/// Init code that immediately reverts (failed contract constructor).
inline bcos::bytes revertInitcode()
{
    return {0x60, 0x00, 0x60, 0x00, 0xfd};  // PUSH0 PUSH0 REVERT
}

/// Warms @p other via EXTCODESIZE (evmone access_account in child scope), then REVERT.
inline bcos::bytes revertInitcodeAfterWarmOtherBytecode(evmc_address const& other)
{
    auto code = warmAccountExtCodeSizeBytecode(other);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0xfd);
    return code;
}

/// EXTCODESIZE @p warmTarget, CALL @p inner, STOP (parent keeps running after child revert).
inline bcos::bytes warmAddressThenCallBytecode(
    evmc_address const& warmTarget, evmc_address const& inner, int64_t callGas = 500'000)
{
    auto code = warmAccountExtCodeSizeBytecode(warmTarget);
    detail::appendCallOpcode(code, inner, callGas);
    code.push_back(0x00);  // STOP
    return code;
}

// --- Feature profile factories (mirror bcos-executor CompatFeatureProfile + TE genesis) ---

inline bcos::ledger::Features makeFeaturesPragueEip2929()
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_prague);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);
    return features;
}

inline bcos::ledger::Features makeFeaturesCancunEip2929()
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_cancun);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);
    return features;
}

/// Shanghai-level genesis + EIP-2929, without feature_evm_cancun (0x0a exclusion matrix).
inline bcos::ledger::Features makeFeaturesShanghaiEip2929()
{
    bcos::ledger::Features features;
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);
    return features;
}

inline bcos::ledger::Features makeFeaturesOsakaEip2929()
{
    auto features = makeFeaturesPragueEip2929();
    features.set(bcos::ledger::Features::Flag::feature_evm_osaka);
    return features;
}

// --- EIP-2930 access list builders ---

inline bcos::executor::Eip2930AccessList makeAccessListSingleAccountMultiSlot(
    std::string_view addrHex, std::vector<bcos::h256> const& keys)
{
    return {{std::string(addrHex), keys}};
}

inline bcos::executor::Eip2930AccessList makeAccessListMultiAccount(
    std::vector<std::pair<std::string, std::vector<bcos::h256>>> entries)
{
    return bcos::executor::Eip2930AccessList{std::move(entries)};
}

// --- CREATE2 message builder (caller must keep @p initcode storage alive) ---

inline evmc_message makeCreate2Message(
    evmc_address sender, evmc_bytes32 salt, bcos::bytesConstRef initcode, int64_t gas)
{
    evmc_message message{};
    message.kind = EVMC_CREATE2;
    message.flags = 0;
    message.depth = 0;
    message.gas = gas;
    message.recipient = {};
    message.sender = sender;
    message.input_data = initcode.data();
    message.input_size = initcode.size();
    message.value = {};
    message.create2_salt = salt;
    message.code_address = {};
    message.code = nullptr;
    message.code_size = 0;
    message.destination_ptr = nullptr;
    message.destination_len = 0;
    message.sender_ptr = nullptr;
    message.sender_len = 0;
    return message;
}

// --- Gas probe: double EXTCODESIZE on same address via evmone execute() ---

namespace detail
{
template <class Fixture>
bcos::task::Task<int64_t> measureProbeGasTask(Fixture& fixture,
    bcos::ledger::Features const& features, bcos::bytes const& code, uint8_t runnerTag,
    int64_t startGas)
{
    evmc_address origin{};
    origin.bytes[19] = 0x70;
    evmc_address runner{};
    runner.bytes[19] = runnerTag;

    bcos::ledger::account::EVMAccount<decltype(fixture.rollbackableStorage)> originAcc(
        fixture.rollbackableStorage, origin, false);
    if (!co_await originAcc.exists())
    {
        co_await originAcc.create();
    }
    co_await originAcc.setBalance(bcos::u256(1) << 96);

    bcos::ledger::account::EVMAccount<decltype(fixture.rollbackableStorage)> runnerAcc(
        fixture.rollbackableStorage, runner, false);
    if (!co_await runnerAcc.exists())
    {
        co_await runnerAcc.create();
    }
    auto const hash = fixture.hashImpl->hash(bcos::bytesConstRef(code.data(), code.size()));
    co_await runnerAcc.setCode(code, "", hash);

    auto host =
        fixture.makeHost(features, static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION),
            origin, runner, EVMC_CALL, {}, 0, startGas);
    host.mutableMessage().code_address = runner;
    co_await host.prepare();
    auto const result = co_await host.execute();
    if (result.status_code != EVMC_SUCCESS)
    {
        co_return -1;
    }
    co_return startGas - result.gas_left;
}

template <class Fixture>
bcos::task::Task<int64_t> measureDoubleExtCodeSizeGasTask(Fixture& fixture,
    bcos::ledger::Features const& features, evmc_address const& target, int64_t startGas,
    uint8_t runnerTag)
{
    co_return co_await measureProbeGasTask(
        fixture, features, doubleExtCodeSizeBytecode(target), runnerTag, startGas);
}

template <class Fixture>
bcos::task::Task<int64_t> measureTwoAccountsExtCodeSizeGasTask(Fixture& fixture,
    bcos::ledger::Features const& features, evmc_address const& target1,
    evmc_address const& target2, int64_t startGas, uint8_t runnerTag)
{
    auto code = warmTwoAccountsExtCodeSizeBytecode(target1, target2);
    code.push_back(0x00);  // STOP
    co_return co_await measureProbeGasTask(fixture, features, code, runnerTag, startGas);
}
}  // namespace detail

/// Returns gas consumed by bytecode that does EXTCODESIZE twice on @p target, or -1 on failure.
template <class Fixture>
int64_t measureDoubleExtCodeSizeGas(Fixture& fixture, bcos::ledger::Features const& features,
    evmc_address const& target, int64_t startGas = 2'000'000, uint8_t runnerTag = 0x71)
{
    return bcos::task::syncWait(
        detail::measureDoubleExtCodeSizeGasTask(fixture, features, target, startGas, runnerTag));
}

/// Two EXTCODESIZE on different addresses (both cold) — for cold/warm gas contrast.
template <class Fixture>
int64_t measureTwoAccountsExtCodeSizeGas(Fixture& fixture, bcos::ledger::Features const& features,
    evmc_address const& target1, evmc_address const& target2, int64_t startGas = 2'000'000,
    uint8_t runnerTag = 0x72)
{
    return bcos::task::syncWait(detail::measureTwoAccountsExtCodeSizeGasTask(
        fixture, features, target1, target2, startGas, runnerTag));
}

}  // namespace bcos::test::eip2929
