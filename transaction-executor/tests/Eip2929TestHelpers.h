/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared bytecode helpers for EIP-2929 transaction-executor tests.
 */

#pragma once

#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.h>
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

/// Outer: CALL child, then REVERT. @p inner must already hold code.
/// Uses a fixed call gas stipend so the outer frame retains gas for REVERT.
inline bcos::bytes callThenRevertBytecode(evmc_address const& inner, int64_t callGas = 500'000)
{
    bcos::bytes code;
    // PUSH20 inner
    code.push_back(0x73);
    code.insert(code.end(), inner.bytes, inner.bytes + 20);
    // PUSH2/PUSH3 callGas (stack: address, gas, …)
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
    // PUSH0 x5 (value, argsOffset, argsSize, retOffset, retSize)
    for (int i = 0; i < 5; ++i)
    {
        code.push_back(0x60);
        code.push_back(0x00);
    }
    code.push_back(0xf1);  // CALL
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0x60);
    code.push_back(0x00);
    code.push_back(0xfd);  // REVERT
    return code;
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
inline void appendCallOpcode(bcos::bytes& code, evmc_address const& callee, int64_t callGas)
{
    code.push_back(0x73);
    code.insert(code.end(), callee.bytes, callee.bytes + 20);
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
    for (int i = 0; i < 5; ++i)
    {
        code.push_back(0x60);
        code.push_back(0x00);
    }
    code.push_back(0xf1);  // CALL
}
}  // namespace detail

/// EXTCODESIZE @p warmTarget, CALL @p inner, STOP (parent keeps running after child revert).
inline bcos::bytes warmAddressThenCallBytecode(
    evmc_address const& warmTarget, evmc_address const& inner, int64_t callGas = 500'000)
{
    auto code = warmAccountExtCodeSizeBytecode(warmTarget);
    detail::appendCallOpcode(code, inner, callGas);
    code.push_back(0x00);  // STOP
    return code;
}

}  // namespace bcos::test::eip2929
