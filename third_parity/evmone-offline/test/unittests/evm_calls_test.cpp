// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

/// This file contains EVM unit tests that perform any kind of calls.

#include "evm_fixture.hpp"

using namespace evmc::literals;
using evm_calls = evm;

TEST_F(evm_calls, delegatecall)
{
    auto code = bytecode{};
    code += "6001600003600052";              // m[0] = 0xffffff...
    code += "600560046003600260016103e8f4";  // DELEGATECALL(1000, 0x01, ...)
    code += "60086000f3";

    auto call_output = bytes{0xa, 0xb, 0xc};
    host.call_result.output_data = call_output.data();
    host.call_result.output_size = call_output.size();
    host.call_result.gas_left = 1;

    execute(1700, code);

    EXPECT_EQ(gas_used, 1690);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);

    auto gas_left = 1700 - 736;
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, gas_left - gas_left / 64);
    EXPECT_EQ(call_msg.input_size, 3);

    ASSERT_EQ(result.output_size, 8);
    EXPECT_EQ(output, (bytes{0xff, 0xff, 0xff, 0xff, 0xa, 0xb, 0xc, 0xff}));
}

TEST_F(evm_calls, delegatecall_static)
{
    // Checks if DELEGATECALL forwards the "static" flag.
    msg.flags = EVMC_STATIC;
    execute(bytecode{} + delegatecall(0).gas(1));
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 1);
    EXPECT_EQ(call_msg.flags, uint32_t{EVMC_STATIC});
    EXPECT_GAS_USED(EVMC_SUCCESS, 719);
}

TEST_F(evm_calls, create)
{
    auto& account = host.accounts[{}];
    account.set_balance(1);

    auto call_output = bytes{0xa, 0xb, 0xc};
    host.call_result.output_data = call_output.data();
    host.call_result.output_size = call_output.size();
    host.call_result.create_address.bytes[10] = 0xcc;
    host.call_result.gas_left = 200000;
    execute(300000, bytecode{"602060006001f0600155"});

    EXPECT_EQ(gas_used, 115816);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);

    auto key = evmc_bytes32{};
    key.bytes[31] = 1;
    EXPECT_EQ(account.storage[key].value.bytes[22], 0xcc);

    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.input_size, 0x20);
}

TEST_F(evm_calls, create_gas)
{
    auto c = size_t{0};
    for (auto r : {EVMC_HOMESTEAD, EVMC_TANGERINE_WHISTLE})
    {
        ++c;
        rev = r;
        execute(50000, "60008080f0");
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_HOMESTEAD ? 50000 : 49719) << rev;
        ASSERT_EQ(host.recorded_calls.size(), c);
        EXPECT_EQ(host.recorded_calls.back().gas, rev == EVMC_HOMESTEAD ? 17991 : 17710) << rev;
    }
}

TEST_F(evm_calls, create2)
{
    rev = EVMC_CONSTANTINOPLE;
    auto& account = host.accounts[{}];
    account.set_balance(1);

    auto call_output = bytes{0xa, 0xb, 0xc};
    host.call_result.output_data = call_output.data();
    host.call_result.output_size = call_output.size();
    host.call_result.create_address.bytes[10] = 0xc2;
    host.call_result.gas_left = 200000;
    execute(300000, "605a604160006001f5600155");

    EXPECT_EQ(gas_used, 115817);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);


    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.create2_salt.bytes[31], 0x5a);
    EXPECT_EQ(call_msg.gas, 263775);
    EXPECT_EQ(call_msg.kind, EVMC_CREATE2);

    auto key = evmc_bytes32{};
    key.bytes[31] = 1;
    EXPECT_EQ(account.storage[key].value.bytes[22], 0xc2);

    EXPECT_EQ(call_msg.input_size, 0x41);
}

TEST_F(evm_calls, create2_salt_cost)
{
    rev = EVMC_CONSTANTINOPLE;
    auto code = "600060208180f5";


    execute(32021, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().kind, EVMC_CREATE2);
    EXPECT_EQ(host.recorded_calls.back().depth, 1);

    execute(32021 - 1, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(host.recorded_calls.size(), 1);  // No another CREATE2.
}

TEST_F(evm_calls, create_balance_too_low)
{
    rev = EVMC_CONSTANTINOPLE;
    host.accounts[{}].set_balance(1);
    for (auto op : {OP_CREATE, OP_CREATE2})
    {
        execute(push(2) + (3 * OP_DUP1) + hex(op) + ret_top());
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
        EXPECT_EQ(host.recorded_calls.size(), 0);
    }
}

TEST_F(evm_calls, create_failure)
{
    host.call_result.create_address = evmc::address{{0xce}};
    const auto create_address =
        bytes_view{host.call_result.create_address.bytes, sizeof(host.call_result.create_address)};
    rev = EVMC_CONSTANTINOPLE;
    for (auto op : {OP_CREATE, OP_CREATE2})
    {
        const auto code = push(0) + (3 * OP_DUP1) + op + ret_top();

        host.call_result.status_code = EVMC_SUCCESS;
        execute(code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 32);
        EXPECT_EQ((bytes_view{result.output_data + 12, 20}), create_address);
        ASSERT_EQ(host.recorded_calls.size(), 1);
        EXPECT_EQ(host.recorded_calls.back().kind, op == OP_CREATE ? EVMC_CREATE : EVMC_CREATE2);
        host.recorded_calls.clear();

        host.call_result.status_code = EVMC_REVERT;
        execute(code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_OUTPUT_INT(0);
        ASSERT_EQ(host.recorded_calls.size(), 1);
        EXPECT_EQ(host.recorded_calls.back().kind, op == OP_CREATE ? EVMC_CREATE : EVMC_CREATE2);
        host.recorded_calls.clear();

        host.call_result.status_code = EVMC_FAILURE;
        execute(code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_OUTPUT_INT(0);
        ASSERT_EQ(host.recorded_calls.size(), 1);
        EXPECT_EQ(host.recorded_calls.back().kind, op == OP_CREATE ? EVMC_CREATE : EVMC_CREATE2);
        host.recorded_calls.clear();
    }
}

TEST_F(evm_calls, call_failing_with_value)
{
    host.accounts[0x00000000000000000000000000000000000000aa_address] = {};
    for (auto op : {OP_CALL, OP_CALLCODE})
    {
        const auto code = push(0xff) + push(0) + OP_DUP2 + OP_DUP2 + push(1) + push(0xaa) +
                          push(0x8000) + op + OP_POP;

        // Fails on balance check.
        execute(12000, code);
        EXPECT_GAS_USED(EVMC_SUCCESS, 7447);
        EXPECT_EQ(host.recorded_calls.size(), 0);  // There was no call().

        // Fails on value transfer additional cost - minimum gas limit that triggers this condition.
        execute(747, code);
        EXPECT_STATUS(EVMC_OUT_OF_GAS);
        EXPECT_EQ(host.recorded_calls.size(), 0);  // There was no call().

        // Fails on value transfer additional cost - maximum gas limit that triggers this condition.
        execute(744 + 9000, code);
        EXPECT_STATUS(EVMC_OUT_OF_GAS);
        EXPECT_EQ(host.recorded_calls.size(), 0);  // There was no call().
    }
}

TEST_F(evm_calls, call_with_value)
{
    auto code = "60ff600060ff6000600160aa618000f150";

    host.accounts[{}].set_balance(1);
    auto call_dst = evmc::address{};
    call_dst.bytes[19] = 0xaa;
    host.accounts[call_dst] = {};
    host.call_result.gas_left = 1;

    execute(40000, code);
    EXPECT_EQ(gas_used, 7447 + 32082);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALL);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, 32083);
    EXPECT_EQ(call_msg.destination, call_dst);
}

TEST_F(evm_calls, call_with_value_depth_limit)
{
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    host.accounts[call_dst] = {};

    msg.depth = 1024;
    execute(bytecode{"60ff600060ff6000600160aa618000f150"});
    EXPECT_EQ(gas_used, 7447);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(host.recorded_calls.size(), 0);
}

TEST_F(evm_calls, call_depth_limit)
{
    rev = EVMC_CONSTANTINOPLE;
    msg.depth = 1024;

    for (auto op : {OP_CALL, OP_CALLCODE, OP_DELEGATECALL, OP_STATICCALL, OP_CREATE, OP_CREATE2})
    {
        const auto code = push(0) + 6 * OP_DUP1 + op + ret_top() + OP_INVALID;
        execute(code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(host.recorded_calls.size(), 0);
        EXPECT_OUTPUT_INT(0);
    }
}

TEST_F(evm_calls, call_output)
{
    static bool result_is_correct = false;
    static uint8_t call_output[] = {0xa, 0xb};

    host.accounts[{}].set_balance(1);
    host.call_result.output_data = call_output;
    host.call_result.output_size = sizeof(call_output);
    host.call_result.release = [](const evmc_result* r) {
        result_is_correct = r->output_size == sizeof(call_output) && r->output_data == call_output;
    };

    auto code_prefix_output_1 = push(1) + 6 * OP_DUP1 + push("7fffffffffffffff");
    auto code_prefix_output_0 = push(0) + 6 * OP_DUP1 + push("7fffffffffffffff");
    auto code_suffix = ret(0, 3);

    for (auto op : {OP_CALL, OP_CALLCODE, OP_DELEGATECALL, OP_STATICCALL})
    {
        result_is_correct = false;
        execute(code_prefix_output_1 + hex(op) + code_suffix);
        EXPECT_TRUE(result_is_correct);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 3);
        EXPECT_EQ(result.output_data[0], 0);
        EXPECT_EQ(result.output_data[1], 0xa);
        EXPECT_EQ(result.output_data[2], 0);


        result_is_correct = false;
        execute(code_prefix_output_0 + hex(op) + code_suffix);
        EXPECT_TRUE(result_is_correct);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 3);
        EXPECT_EQ(result.output_data[0], 0);
        EXPECT_EQ(result.output_data[1], 0);
        EXPECT_EQ(result.output_data[2], 0);
    }
}

TEST_F(evm_calls, call_high_gas)
{
    rev = EVMC_HOMESTEAD;
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    host.accounts[call_dst] = {};

    for (auto call_opcode : {"f1", "f2", "f4"})
    {
        execute(5000, 5 * push(0) + push(0xaa) + push(0x134c) + call_opcode);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    }
}

TEST_F(evm_calls, call_value_zero_to_nonexistent_account)
{
    constexpr auto call_gas = 6000;
    host.call_result.gas_left = 1000;

    const auto code = push(0x40) + push(0) + push(0x40) + push(0) + push(0) + push(0xaa) +
                      push(call_gas) + OP_CALL + OP_POP;

    execute(9000, code);
    EXPECT_EQ(gas_used, 729 + (call_gas - host.call_result.gas_left));
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALL);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, 6000);
}

TEST_F(evm_calls, call_new_account_creation_cost)
{
    const auto call_dst = evmc::address{{0xad}};
    const auto code = 4 * push(0) + calldataload(0) + push({call_dst.bytes, sizeof(call_dst)}) +
                      push(0) + OP_CALL + ret_top();
    msg.destination = evmc_address{{3}};


    rev = EVMC_TANGERINE_WHISTLE;
    host.accounts[msg.destination].set_balance(0);
    execute(code, "00");
    EXPECT_GAS_USED(EVMC_SUCCESS, 25000 + 739);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().destination, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 0);
    ASSERT_EQ(host.recorded_account_accesses.size(), 2);
    EXPECT_EQ(host.recorded_account_accesses[0], call_dst);  // Account exist?
    EXPECT_EQ(host.recorded_account_accesses[1], call_dst);  // Call.
    host.recorded_account_accesses.clear();
    host.recorded_calls.clear();

    rev = EVMC_TANGERINE_WHISTLE;
    host.accounts[msg.destination].set_balance(1);
    execute(code, "0000000000000000000000000000000000000000000000000000000000000001");
    EXPECT_GAS_USED(EVMC_SUCCESS, 25000 + 9000 + 739);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().destination, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);
    ASSERT_EQ(host.recorded_account_accesses.size(), 3);
    EXPECT_EQ(host.recorded_account_accesses[0], call_dst);         // Account exist?
    EXPECT_EQ(host.recorded_account_accesses[1], msg.destination);  // Balance.
    EXPECT_EQ(host.recorded_account_accesses[2], call_dst);         // Call.
    host.recorded_account_accesses.clear();
    host.recorded_calls.clear();

    rev = EVMC_SPURIOUS_DRAGON;
    host.accounts[msg.destination].set_balance(0);
    execute(code, "00");
    EXPECT_GAS_USED(EVMC_SUCCESS, 739);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().destination, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 0);
    ASSERT_EQ(host.recorded_account_accesses.size(), 1);
    EXPECT_EQ(host.recorded_account_accesses[0], call_dst);  // Call.
    host.recorded_account_accesses.clear();
    host.recorded_calls.clear();

    rev = EVMC_SPURIOUS_DRAGON;
    host.accounts[msg.destination].set_balance(1);
    execute(code, "0000000000000000000000000000000000000000000000000000000000000001");
    EXPECT_GAS_USED(EVMC_SUCCESS, 25000 + 9000 + 739);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().destination, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);
    ASSERT_EQ(host.recorded_account_accesses.size(), 3);
    EXPECT_EQ(host.recorded_account_accesses[0], call_dst);         // Account exist?
    EXPECT_EQ(host.recorded_account_accesses[1], msg.destination);  // Balance.
    EXPECT_EQ(host.recorded_account_accesses[2], call_dst);         // Call.
    host.recorded_account_accesses.clear();
    host.recorded_calls.clear();
}

TEST_F(evm_calls, callcode_new_account_create)
{
    auto code = "60008080806001600061c350f250";

    host.accounts[{}].set_balance(1);
    host.call_result.gas_left = 1;
    execute(100000, code);
    EXPECT_EQ(gas_used, 59722);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALLCODE);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, 52300);
}

TEST_F(evm_calls, call_then_oog)
{
    // Performs a CALL then OOG in the same code block.
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    host.accounts[call_dst] = {};
    host.call_result.status_code = EVMC_FAILURE;
    host.call_result.gas_left = 0;

    const auto code =
        call(0xaa).gas(254).value(0).input(0, 0x40).output(0, 0x40) + 4 * add(OP_DUP1) + OP_POP;

    execute(1000, code);
    EXPECT_EQ(gas_used, 1000);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 254);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_calls, callcode_then_oog)
{
    // Performs a CALLCODE then OOG in the same code block.
    host.call_result.status_code = EVMC_FAILURE;
    host.call_result.gas_left = 0;

    const auto code =
        callcode(0xaa).gas(100).value(0).input(0, 3).output(3, 9) + 4 * add(OP_DUP1) + OP_POP;

    execute(825, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 100);
}

TEST_F(evm_calls, delegatecall_then_oog)
{
    // Performs a CALL then OOG in the same code block.
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    host.accounts[call_dst] = {};
    host.call_result.status_code = EVMC_FAILURE;
    host.call_result.gas_left = 0;
    execute(1000, "604060006040600060aa60fef4800180018001800150");
    EXPECT_EQ(gas_used, 1000);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 254);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_calls, staticcall_then_oog)
{
    // Performs a STATICCALL then OOG in the same code block.
    auto call_dst = evmc_address{};
    call_dst.bytes[19] = 0xaa;
    host.accounts[call_dst] = {};
    host.call_result.status_code = EVMC_FAILURE;
    host.call_result.gas_left = 0;

    const auto code =
        staticcall(0xaa).gas(254).input(0, 0x40).output(0, 0x40) + 4 * add(OP_DUP1) + OP_POP;

    execute(1000, code);
    EXPECT_EQ(gas_used, 1000);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 254);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_calls, staticcall_input)
{
    const auto code = mstore(3, 0x010203) + staticcall(0).gas(0xee).input(32, 3);
    execute(code);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, 0xee);
    EXPECT_EQ(call_msg.input_size, 3);
    EXPECT_EQ(hex(bytes_view(call_msg.input_data, call_msg.input_size)), "010203");
}

TEST_F(evm_calls, call_with_value_low_gas)
{
    // Create the call destination account.
    host.accounts[0x0000000000000000000000000000000000000000_address] = {};
    for (auto call_op : {OP_CALL, OP_CALLCODE})
    {
        auto code = 4 * push(0) + push(1) + 2 * push(0) + call_op + OP_POP;
        execute(9721, code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(result.gas_left, 2300 - 2);
    }
}

TEST_F(evm_calls, call_oog_after_balance_check)
{
    // Create the call destination account.
    host.accounts[0x0000000000000000000000000000000000000000_address] = {};
    for (auto op : {OP_CALL, OP_CALLCODE})
    {
        auto code = 4 * push(0) + push(1) + 2 * push(0) + op + OP_SELFDESTRUCT;
        execute(12420, code);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    }
}

TEST_F(evm_calls, call_oog_after_depth_check)
{
    // Create the call destination account.
    host.accounts[0x0000000000000000000000000000000000000000_address] = {};
    msg.depth = 1024;

    for (auto op : {OP_CALL, OP_CALLCODE})
    {
        const auto code = 4 * push(0) + push(1) + 2 * push(0) + op + OP_SELFDESTRUCT;
        execute(12420, code);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    }

    rev = EVMC_TANGERINE_WHISTLE;
    const auto code = 7 * push(0) + OP_CALL + OP_SELFDESTRUCT;
    execute(721, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);

    execute(721 + 5000 - 1, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_calls, create_oog_after)
{
    rev = EVMC_CONSTANTINOPLE;
    for (auto op : {OP_CREATE, OP_CREATE2})
    {
        auto code = 4 * push(0) + op + OP_SELFDESTRUCT;
        execute(39000, code);
        EXPECT_STATUS(EVMC_OUT_OF_GAS);
    }
}

TEST_F(evm_calls, returndatasize_before_call)
{
    execute("3d60005360016000f3");
    EXPECT_EQ(gas_used, 17);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm_calls, returndatasize)
{
    uint8_t call_output[13];
    host.call_result.output_size = std::size(call_output);
    host.call_result.output_data = std::begin(call_output);

    auto code = "60008080808080f43d60005360016000f3";
    execute(code);
    EXPECT_EQ(gas_used, 735);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], std::size(call_output));

    host.call_result.output_size = 1;
    host.call_result.status_code = EVMC_FAILURE;
    execute(code);
    EXPECT_EQ(gas_used, 735);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 1);

    host.call_result.output_size = 0;
    host.call_result.status_code = EVMC_INTERNAL_ERROR;
    execute(code);
    EXPECT_EQ(gas_used, 735);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm_calls, returndatacopy)
{
    uint8_t call_output[32] = {1, 2, 3, 4, 5, 6, 7};
    host.call_result.output_size = std::size(call_output);
    host.call_result.output_data = std::begin(call_output);

    auto code = "600080808060aa60fff4506020600060003e60206000f3";
    execute(code);
    EXPECT_EQ(gas_used, 999);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[0], 1);
    EXPECT_EQ(result.output_data[1], 2);
    EXPECT_EQ(result.output_data[2], 3);
    EXPECT_EQ(result.output_data[6], 7);
    EXPECT_EQ(result.output_data[7], 0);
}

TEST_F(evm_calls, returndatacopy_empty)
{
    auto code = "600080808060aa60fff4600080803e60016000f3";
    execute(code);
    EXPECT_EQ(gas_used, 994);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm_calls, returndatacopy_cost)
{
    auto call_output = uint8_t{};
    host.call_result.output_data = &call_output;
    host.call_result.output_size = sizeof(call_output);
    auto code = "60008080808080fa6001600060003e";
    execute(736, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(735, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_calls, returndatacopy_outofrange)
{
    auto call_output = uint8_t{};
    host.call_result.output_data = &call_output;
    host.call_result.output_size = sizeof(call_output);
    execute(735, "60008080808080fa6002600060003e");
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

    execute(735, "60008080808080fa6001600160003e");
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

    execute(735, "60008080808080fa6000600260003e");
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);
}
