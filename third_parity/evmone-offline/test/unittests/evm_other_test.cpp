// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

/// This file contains non-mainstream EVM unit tests not matching any concrete category:
/// - regression tests,
/// - tests from fuzzers,
/// - evmone's internal tests.

#include "evm_fixture.hpp"

#include <evmone/limits.hpp>

using evm_other = evm;

TEST_F(evm_other, evmone_loaded_program_relocation)
{
    // The bytecode of size 2 will create evmone's loaded program of size 4 and will cause
    // the relocation of the C++ vector containing the program instructions.
    execute(bytecode{} + OP_STOP + OP_ORIGIN);
    EXPECT_GAS_USED(EVMC_SUCCESS, 0);
}

TEST_F(evm_other, evmone_block_stack_req_overflow)
{
    // This tests constructs a code with single basic block which stack requirement is > int16 max.
    // Such basic block can cause int16_t overflow during analysis.
    // The CALL instruction is going to be used because it has -6 stack change.

    const auto code = push(1) + 10 * OP_DUP1 + 5463 * OP_CALL;
    execute(code);
    EXPECT_STATUS(EVMC_STACK_UNDERFLOW);

    execute(code + ret_top());  // A variant with terminator.
    EXPECT_STATUS(EVMC_STACK_UNDERFLOW);
}

TEST_F(evm_other, evmone_block_max_stack_growth_overflow)
{
    // This tests constructs a code with single basic block which stack max growth is > int16 max.
    // Such basic block can cause int16_t overflow during analysis.

    constexpr auto test_max_code_size = 1024 * 1024u + 1;

    bytes code_buffer(test_max_code_size, uint8_t{OP_MSIZE});

    for (auto max_stack_growth : {32767u, 32768u, 65535u, 65536u, test_max_code_size - 1})
    {
        execute({code_buffer.data(), max_stack_growth});
        EXPECT_STATUS(EVMC_STACK_OVERFLOW);

        code_buffer[max_stack_growth] = OP_JUMPDEST;
        execute({code_buffer.data(), max_stack_growth + 1});
        EXPECT_STATUS(EVMC_STACK_OVERFLOW);

        code_buffer[max_stack_growth] = OP_STOP;
        execute({code_buffer.data(), max_stack_growth + 1});
        EXPECT_STATUS(EVMC_STACK_OVERFLOW);

        code_buffer[max_stack_growth] = OP_MSIZE;  // Restore original opcode.
    }
}

TEST_F(evm_other, evmone_block_gas_cost_overflow_create)
{
    // The goal is to build bytecode with as many CREATE instructions (the most expensive one)
    // as possible but with having balanced stack.
    // The runtime values of arguments are not important.

    constexpr auto gas_max = std::numeric_limits<uint32_t>::max();
    constexpr auto n = gas_max / 32006 + 1;

    auto code = bytecode{OP_MSIZE};
    code.reserve(3 * n);
    for (uint32_t i = 0; i < n; ++i)
    {
        code.push_back(OP_DUP1);
        code.push_back(OP_DUP1);
        code.push_back(OP_CREATE);
    }
    EXPECT_EQ(code.size(), 402'580);

    execute(0, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_TRUE(host.recorded_calls.empty());
    host.recorded_calls.clear();

    execute(gas_max - 1, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_TRUE(host.recorded_calls.empty());
}

TEST_F(evm_other, evmone_block_gas_cost_overflow_balance)
{
    // Here we build single-block bytecode with as many BALANCE instructions as possible.

    rev = EVMC_ISTANBUL;  // Here BALANCE costs 700.

    constexpr auto gas_max = std::numeric_limits<uint32_t>::max();
    constexpr auto n = gas_max / 700 + 2;
    auto code = bytecode{bytes(n, OP_BALANCE)};
    code[0] = OP_ADDRESS;
    EXPECT_EQ(code.size(), 6'135'669);

    execute(0, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_TRUE(host.recorded_account_accesses.empty());
    host.recorded_account_accesses.clear();

    execute(gas_max - 1, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_TRUE(host.recorded_account_accesses.empty());
}

TEST_F(evm_other, loop_full_of_jumpdests)
{
    // The code is a simple loop with a counter taken from the input or a constant (325) if the
    // input is zero. The loop body contains of only JUMPDESTs, as much as the code size limit
    // allows.

    // The `mul(325, iszero(dup1(calldataload(0)))) + OP_OR` is equivalent of
    // `((x == 0) * 325) | x`
    // what is
    // `x == 0 ? 325 : x`.

    // The `not_(0)` is -1 so we can do `loop_counter + (-1)` to decrease the loop counter.

    const auto code = push(15) + not_(0) + mul(325, iszero(dup1(calldataload(0)))) + OP_OR +
                      (max_code_size - 20) * OP_JUMPDEST + OP_DUP2 + OP_ADD + OP_DUP1 + OP_DUP4 +
                      OP_JUMPI;

    EXPECT_EQ(code.size(), max_code_size);

    execute(code);
    EXPECT_GAS_USED(EVMC_SUCCESS, 7987882);
}

TEST_F(evm_other, jumpdest_with_high_offset)
{
    for (auto offset : {3u, 16383u, 16384u, 32767u, 32768u, 65535u, 65536u})
    {
        auto code = push(offset) + OP_JUMP;
        code.resize(offset, OP_INVALID);
        code.push_back(OP_JUMPDEST);
        execute(code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS) << "JUMPDEST at " << offset;
    }
}