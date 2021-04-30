// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include "evm_fixture.hpp"
#include <evmc/instructions.h>
#include <evmone/limits.hpp>
#include <intx/intx.hpp>
#include <algorithm>
#include <numeric>

using namespace evmc::literals;
using namespace intx;

TEST_F(evm, empty)
{
    execute(0, bytes_view{});
    EXPECT_GAS_USED(EVMC_SUCCESS, 0);
}

TEST_F(evm, push_and_pop)
{
    execute(11, push("0102") + OP_POP + push("010203040506070809") + OP_POP);
    EXPECT_GAS_USED(EVMC_SUCCESS, 10);
}

TEST_F(evm, push_implicit_data)
{
    // This test executes 1 byte code with a push instruction without the push data following.
    // Unfortunately, there is no result we could observe other than program crash.

    // Create long bytecode prefix to force the bytecode to be stored on the heap which
    // enables invalid heap access detection via memory access validation tooling (e.g. Valgrind).
    auto code = bytecode{} + OP_PC + OP_GAS + 100 * OP_SWAP1 + OP_STOP;

    for (auto op = uint8_t{OP_PUSH1}; op <= OP_PUSH32; ++op)
    {
        code.back() = op;
        execute(code);
    }
}

TEST_F(evm, stack_underflow)
{
    execute(13, push(1) + OP_POP + push(1) + OP_POP + OP_POP);
    EXPECT_STATUS(EVMC_STACK_UNDERFLOW);

    execute(bytecode{OP_NOT});
    EXPECT_STATUS(EVMC_STACK_UNDERFLOW);
}

TEST_F(evm, add)
{
    execute(25, bytecode{"6007600d0160005260206000f3"});
    EXPECT_GAS_USED(EVMC_SUCCESS, 24);
    EXPECT_OUTPUT_INT(20);
}

TEST_F(evm, dup)
{
    // 0 7 3 5
    // 0 7 3 5 3 5
    // 0 7 3 5 3 5 5 7
    // 0 7 3 5 20
    // 0 7 3 5 (20 0)
    // 0 7 3 5 3 0
    execute("6000600760036005818180850101018452602084f3");
    EXPECT_GAS_USED(EVMC_SUCCESS, 48);
    EXPECT_OUTPUT_INT(20);
}

TEST_F(evm, dup_all_1)
{
    execute(push(1) + "808182838485868788898a8b8c8d8e8f" + "01010101010101010101010101010101" +
            ret_top());
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_OUTPUT_INT(17);
}

TEST_F(evm, dup_stack_overflow)
{
    auto code = push(1) + "808182838485868788898a8b8c8d8e8f";
    for (int i = 0; i < (1024 - 17); ++i)
        code += "8f";

    execute(code);
    EXPECT_STATUS(EVMC_SUCCESS);
    execute(code + "8f");
    EXPECT_STATUS(EVMC_STACK_OVERFLOW);
}

TEST_F(evm, sub_and_swap)
{
    execute(33, "600180810380829052602090f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[31], 1);
}

TEST_F(evm, swapsn_jumpdest)
{
    // Test demonstrating possible problem with introducing multibyte SWAP/DUP instructions as per
    // EIP-663 variants B and C.
    // When SWAPSN is implemented execution will fail with EVMC_BAD_JUMP_DESTINATION.
    const auto swapsn = "b3";
    const auto code = push(4) + OP_JUMP + swapsn + OP_JUMPDEST + push(0) + ret_top();

    rev = EVMC_PETERSBURG;
    execute(code);
    EXPECT_STATUS(EVMC_SUCCESS);

    rev = EVMC_ISTANBUL;
    execute(code);
    EXPECT_STATUS(EVMC_SUCCESS);

    rev = EVMC_MAX_REVISION;
    execute(code);
    EXPECT_STATUS(EVMC_SUCCESS);
}

TEST_F(evm, swapsn_push)
{
    // Test demonstrating possible problem with introducing multibyte SWAP/DUP instructions as per
    // EIP-663 variants B and C.
    // When SWAPSN is implemented execution will succeed, considering PUSH an argument of SWAPSN.
    const auto swapsn = "b3";
    const auto code = push(5) + OP_JUMP + swapsn + push(OP_JUMPDEST) + push(0) + ret_top();

    rev = EVMC_PETERSBURG;
    execute(code);
    EXPECT_STATUS(EVMC_BAD_JUMP_DESTINATION);

    rev = EVMC_ISTANBUL;
    execute(code);
    EXPECT_STATUS(EVMC_BAD_JUMP_DESTINATION);

    rev = EVMC_MAX_REVISION;
    execute(code);
    EXPECT_STATUS(EVMC_BAD_JUMP_DESTINATION);
}

TEST_F(evm, memory_and_not)
{
    execute(42, "600060018019815381518252800190f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 2);
    EXPECT_EQ(result.output_data[1], 0xfe);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm, msize)
{
    execute(29, "60aa6022535960005360016000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0x40);
}

TEST_F(evm, gas)
{
    execute(40, "5a5a5a010160005360016000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 13);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 38 + 36 + 34);
}

TEST_F(evm, arith)
{
    // x = (0 - 1) * 3
    // y = 17 s/ x
    // z = 17 s% x
    // a = 17 * x + z
    // iszero
    std::string s;
    s += "60116001600003600302";  // 17 -3
    s += "808205";                // 17 -3 -5
    s += "818307";                // 17 -3 -5 2
    s += "910201";                // 17 17
    s += "0315";                  // 1
    s += "60005360016000f3";
    execute(100, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 26);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 1);
}

TEST_F(evm, comparison)
{
    std::string s;
    s += "60006001808203808001";  // 0 1 -1 -2
    s += "828210600053";          // m[0] = -1 < 1
    s += "828211600153";          // m[1] = -1 > 1
    s += "828212600253";          // m[2] = -1 s< 1
    s += "828213600353";          // m[3] = -1 s> 1
    s += "828214600453";          // m[4] = -1 == 1
    s += "818112600553";          // m[5] = -2 s< -1
    s += "818113600653";          // m[6] = -2 s> -1
    s += "60076000f3";
    execute(s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 138);
    ASSERT_EQ(result.output_size, 7);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[1], 1);
    EXPECT_EQ(result.output_data[2], 1);
    EXPECT_EQ(result.output_data[3], 0);
    EXPECT_EQ(result.output_data[4], 0);
    EXPECT_EQ(result.output_data[5], 1);
    EXPECT_EQ(result.output_data[6], 0);
}

TEST_F(evm, bitwise)
{
    std::string s;
    s += "60aa60ff";      // aa ff
    s += "818116600053";  // m[0] = aa & ff
    s += "818117600153";  // m[1] = aa | ff
    s += "818118600253";  // m[2] = aa ^ ff
    s += "60036000f3";
    execute(60, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 3);
    EXPECT_EQ(result.output_data[0], 0xaa & 0xff);
    EXPECT_EQ(result.output_data[1], 0xaa | 0xff);
    EXPECT_EQ(result.output_data[2], 0xaa ^ 0xff);
}

TEST_F(evm, jump)
{
    std::string s;
    s += "60be600053";  // m[0] = be
    s += "60fa";        // fa
    s += "60055801";    // PC + 5
    s += "56";          // JUMP
    s += "5050";        // POP x2
    s += "5b";          // JUMPDEST
    s += "600153";      // m[1] = fa
    s += "60026000f3";  // RETURN(0,2)
    execute(44, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 2);
    EXPECT_EQ(result.output_data[0], 0xbe);
    EXPECT_EQ(result.output_data[1], 0xfa);
}

TEST_F(evm, jumpi)
{
    std::string s;
    s += "5a600557";      // GAS 5 JUMPI
    s += "00";            // STOP
    s += "5b60016000f3";  // JUMPDEST RETURN(0,1)
    execute(25, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm, jumpi_else)
{
    execute(15, dup1(OP_COINBASE) + OP_JUMPI);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(result.output_size, 0);
}

TEST_F(evm, jumpi_at_the_end)
{
    execute(1000, "5b6001600057");
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    EXPECT_EQ(gas_used, 1000);
}

TEST_F(evm, bad_jumpdest)
{
    host.tx_context.block_number = 1;
    host.tx_context.block_gas_limit = 0;
    host.tx_context.block_timestamp = 0x80000000;
    for (auto op : {OP_JUMP, OP_JUMPI})
    {
        execute("4345" + hex(op));
        EXPECT_EQ(result.status_code, EVMC_BAD_JUMP_DESTINATION);
        EXPECT_EQ(result.gas_left, 0);

        execute("4342" + hex(op));
        EXPECT_EQ(result.status_code, EVMC_BAD_JUMP_DESTINATION);
        EXPECT_EQ(result.gas_left, 0);
    }
}

TEST_F(evm, jump_to_block_beginning)
{
    const auto code = jumpi(0, OP_MSIZE) + jump(4);
    execute(code);
    EXPECT_STATUS(EVMC_BAD_JUMP_DESTINATION);
}

TEST_F(evm, jumpi_stack)
{
    const auto code = push(0xde) + jumpi(6, OP_CALLDATASIZE) + OP_JUMPDEST + ret_top();
    execute(code, "");
    EXPECT_OUTPUT_INT(0xde);
    execute(code, "ee");
    EXPECT_OUTPUT_INT(0xde);
}

TEST_F(evm, jump_over_jumpdest)
{
    // The code contains 2 consecutive JUMPDESTs. The JUMP at the beginning lands on the second one.
    const auto code = push(4) + OP_JUMP + 2 * OP_JUMPDEST;
    execute(code);
    EXPECT_GAS_USED(EVMC_SUCCESS, 3 + 8 + 1);
}

TEST_F(evm, pc)
{
    const auto code = OP_CALLDATASIZE + push(9) + OP_JUMPI + push(12) + OP_PC + OP_SWAP1 + OP_JUMP +
                      OP_JUMPDEST + OP_GAS + OP_PC + OP_JUMPDEST + ret_top();

    execute(code, "");
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_OUTPUT_INT(6);

    execute(code, "ff");
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_OUTPUT_INT(11);
}

TEST_F(evm, byte)
{
    std::string s;
    s += "63aabbccdd";  // aabbccdd
    s += "8060001a";    // DUP 1 BYTE
    s += "600053";      // m[0] = 00
    s += "80601c1a";    // DUP 28 BYTE
    s += "600253";      // m[2] = aa
    s += "80601f1a";    // DUP 31 BYTE
    s += "600453";      // m[4] = dd
    s += "8060201a";    // DUP 32 BYTE
    s += "600653";      // m[6] = 00
    s += "60076000f3";  // RETURN(0,7)
    execute(72, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 7);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[2], 0xaa);
    EXPECT_EQ(result.output_data[4], 0xdd);
    EXPECT_EQ(result.output_data[6], 0);
}

TEST_F(evm, addmod_mulmod)
{
    std::string s;
    s += "7fcdeb8272fc01d4d50a6ec165d2ea477af19b9b2c198459f59079583b97e88a66";
    s += "7f52e7e7a03b86f534d2e338aa1bb05ba3539cb2f51304cdbce69ce2d422c456ca";
    s += "7fe0f2f0cae05c220260e1724bdc66a0f83810bd1217bd105cb2da11e257c6cdf6";
    s += "82828208";    // DUP DUP DUP ADDMOD
    s += "600052";      // m[0..]
    s += "82828209";    // DUP DUP DUP MULMOD
    s += "602052";      // m[32..]
    s += "60406000f3";  // RETURN(0,64)
    execute(67, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 64);
    auto a = from_hex("65ef55f81fe142622955e990252cb5209a11d4db113d842408fd9c7ae2a29a5a");
    EXPECT_EQ(a, bytes(&result.output_data[0], 32));
    auto p = from_hex("34e04890131a297202753cae4c72efd508962c9129aed8b08c8e87ab425b7258");
    EXPECT_EQ(p, bytes(&result.output_data[32], 32));
}

TEST_F(evm, divmod)
{
    // Div and mod the -1 by the input and return.
    execute("600035600160000381810460005281810660205260406000f3", "0d");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 61);
    ASSERT_EQ(result.output_size, 64);
    auto a = from_hex("0000000000000000000000000000000000000000000000000000000000000013");
    EXPECT_EQ(a, bytes(&result.output_data[0], 32));
    auto p = from_hex("08ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    EXPECT_EQ(p, bytes(&result.output_data[32], 32));
}

TEST_F(evm, div_by_zero)
{
    execute(34, dup1(push(0)) + push(0xff) + OP_DIV + OP_SDIV + ret_top());
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_OUTPUT_INT(0);
}

TEST_F(evm, mod_by_zero)
{
    execute(dup1(push(0)) + push(0xeffe) + OP_MOD + OP_SMOD + ret_top());
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 34);
    EXPECT_OUTPUT_INT(0);
}

TEST_F(evm, addmod_mulmod_by_zero)
{
    execute("6000358080808008091560005260206000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 52);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[31], 1);
}

TEST_F(evm, signextend)
{
    std::string s;
    s += "62017ffe";    // 017ffe
    s += "8060000b";    // DUP SIGNEXTEND(0)
    s += "600052";      // m[0..]
    s += "8060010b";    // DUP SIGNEXTEND(1)
    s += "602052";      // m[32..]
    s += "60406000f3";  // RETURN(0,64)
    execute(49, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 64);
    auto a = from_hex("fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
    EXPECT_EQ(bytes(&result.output_data[0], 32), a);
    auto b = from_hex("0000000000000000000000000000000000000000000000000000000000007ffe");
    EXPECT_EQ(bytes(&result.output_data[32], 32), b);
}

TEST_F(evm, signextend_31)
{
    rev = EVMC_CONSTANTINOPLE;

    execute("61010160000360081c601e0b60005260206000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 38);
    ASSERT_EQ(result.output_size, 32);
    auto a = from_hex("fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
    EXPECT_EQ(bytes(&result.output_data[0], 32), a);

    execute("61010160000360081c601f0b60005260206000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 38);
    ASSERT_EQ(result.output_size, 32);
    a = from_hex("00fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe");
    EXPECT_EQ(bytes(&result.output_data[0], 32), a);
}

TEST_F(evm, exp)
{
    std::string s;
    s += "612019";      // 0x2019
    s += "6003";        // 3
    s += "0a";          // EXP
    s += "600052";      // m[0..]
    s += "60206000f3";  // RETURN(0,32)
    execute(131, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 32);
    auto a = from_hex("263cf24662b24c371a647c1340022619306e431bf3a4298d4b5998a3f1c1aaa3");
    EXPECT_EQ(bytes(&result.output_data[0], 32), a);
}

TEST_F(evm, exp_oog)
{
    auto code = "6001600003800a";
    execute(1622, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);

    execute(1621, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_F(evm, exp_pre_spurious_dragon)
{
    rev = EVMC_TANGERINE_WHISTLE;
    std::string s;
    s += "62012019";    // 0x012019
    s += "6003";        // 3
    s += "0a";          // EXP
    s += "600052";      // m[0..]
    s += "60206000f3";  // RETURN(0,32)
    execute(131 - 70, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 32);
    auto a = from_hex("422ea3761c4f6517df7f102bb18b96abf4735099209ca21256a6b8ac4d1daaa3");
    EXPECT_EQ(bytes(&result.output_data[0], 32), a);
}

TEST_F(evm, calldataload)
{
    std::string s;
    s += "600335";      // CALLDATALOAD(3)
    s += "600052";      // m[0..]
    s += "600a6000f3";  // RETURN(0,10)
    execute(21, s, "0102030405");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 10);
    auto a = from_hex("04050000000000000000");
    EXPECT_EQ(bytes(&result.output_data[0], 10), a);
}

TEST_F(evm, calldataload_outofrange)
{
    execute(calldataload(1) + ret_top());
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
}

TEST_F(evm, calldatacopy)
{
    std::string s;
    s += "366001600037";  // CALLDATASIZE 1 0 CALLDATACOPY
    s += "600a6000f3";    // RETURN(0,10)
    execute(s, "0102030405");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 23);
    ASSERT_EQ(result.output_size, 10);
    auto a = from_hex("02030405000000000000");
    EXPECT_EQ(bytes(&result.output_data[0], 10), a);

    execute(s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 20);

    execute("60ff66fffffffffffffa60003760ff6000f3");
    EXPECT_EQ(gas_used, 66);
    ASSERT_EQ(result.output_size, 0xff);
    EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 0xff);
}

TEST_F(evm, address)
{
    std::string s;
    s += "30600052";    // ADDRESS MSTORE(0)
    s += "600a600af3";  // RETURN(10,10)
    msg.destination.bytes[0] = 0xcc;
    execute(17, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 10);
    auto a = from_hex("0000cc00000000000000");
    EXPECT_EQ(bytes(&result.output_data[0], 10), a);
}

TEST_F(evm, caller_callvalue)
{
    std::string s;
    s += "333401600052";  // CALLER CALLVALUE ADD MSTORE(0)
    s += "600a600af3";    // RETURN(10,10)
    msg.sender.bytes[0] = 0xdd;
    msg.value.bytes[13] = 0xee;
    execute(22, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 10);
    auto a = from_hex("0000ddee000000000000");
    EXPECT_EQ(bytes(&result.output_data[0], 10), a);
}

TEST_F(evm, undefined)
{
    execute(1, "2a");
    EXPECT_EQ(result.status_code, EVMC_UNDEFINED_INSTRUCTION);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_F(evm, invalid)
{
    execute(1, "fe");
    EXPECT_EQ(result.status_code, EVMC_INVALID_INSTRUCTION);
    EXPECT_EQ(result.gas_left, 0);
}

TEST_F(evm, sha3)
{
    execute("6108006103ff2060005260206000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 738);
    ASSERT_EQ(result.output_size, 32);
    auto hash = from_hex("aeffb38c06e111d84216396baefeb7fed397f303d5cb84a33f1e8b485c4a22da");
    EXPECT_EQ(bytes(&result.output_data[0], 32), hash);
}

TEST_F(evm, sha3_empty)
{
    auto code = push(0) + OP_DUP1 + OP_SHA3 + ret_top();
    execute(code);
    ASSERT_EQ(result.output_size, 32);
    auto keccak256_empty = "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
    EXPECT_EQ(hex({result.output_data, result.output_size}), keccak256_empty);
}

TEST_F(evm, revert)
{
    std::string s;
    s += "60ee8053";    // m[ee] == e
    s += "600260edfd";  // REVERT(ee,1)
    execute(s);
    EXPECT_EQ(gas_used, 39);
    EXPECT_EQ(result.status_code, EVMC_REVERT);
    ASSERT_EQ(result.output_size, 2);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[1], 0xee);
}

TEST_F(evm, return_empty_buffer_at_high_offset)
{
    host.tx_context.block_difficulty =
        0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff1_bytes32;

    execute(push(0) + OP_DIFFICULTY + OP_RETURN);
    EXPECT_STATUS(EVMC_SUCCESS);

    execute(push(0) + OP_DIFFICULTY + OP_REVERT);
    EXPECT_STATUS(EVMC_REVERT);
}

TEST_F(evm, shl)
{
    auto code = "600560011b6000526001601ff3";
    rev = EVMC_CONSTANTINOPLE;
    execute(code);
    EXPECT_EQ(gas_used, 24);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 5 << 1);
}

TEST_F(evm, shr)
{
    auto code = "600560011c6000526001601ff3";
    rev = EVMC_CONSTANTINOPLE;
    execute(code);
    EXPECT_EQ(gas_used, 24);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 5 >> 1);
}

TEST_F(evm, sar)
{
    auto code = "600160000360021d60005260016000f3";
    rev = EVMC_CONSTANTINOPLE;
    execute(code);
    EXPECT_EQ(gas_used, 30);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0xff);  // MSB of (-1 >> 2) == -1
}

TEST_F(evm, sar_01)
{
    auto code = "600060011d60005260016000f3";
    rev = EVMC_CONSTANTINOPLE;
    execute(code);
    EXPECT_EQ(gas_used, 24);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm, shift_overflow)
{
    rev = EVMC_CONSTANTINOPLE;
    for (auto op : {OP_SHL, OP_SHR, OP_SAR})
    {
        execute(not_(0) + 0x100 + op + ret_top());
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        ASSERT_EQ(result.output_size, 32);
        auto a = std::accumulate(result.output_data, result.output_data + result.output_size, 0);
        EXPECT_EQ(a, op == OP_SAR ? 32 * 0xff : 0);
    }
}

TEST_F(evm, undefined_instructions)
{
    for (auto i = 0; i <= EVMC_MAX_REVISION; ++i)
    {
        auto r = evmc_revision(i);
        auto names = evmc_get_instruction_names_table(r);

        for (uint8_t opcode = 0; opcode <= 0xfe; ++opcode)
        {
            if (names[opcode] != nullptr)
                continue;

            auto res = vm.execute(host, r, {}, &opcode, sizeof(opcode));
            EXPECT_EQ(res.status_code, EVMC_UNDEFINED_INSTRUCTION)
                << " for opcode " << hex(opcode) << " on revision " << r;
        }
    }
}

TEST_F(evm, undefined_instruction_analysis_overflow)
{
    rev = EVMC_PETERSBURG;

    auto undefined_opcode = evmc_opcode(0x0c);
    auto code = bytecode{undefined_opcode};

    execute(code);
    EXPECT_EQ(result.status_code, EVMC_UNDEFINED_INSTRUCTION);
}

TEST_F(evm, undefined_instruction_block_cost_negative)
{
    // For undefined instructions EVMC instruction tables have cost -1.
    // If naively counted block costs can become negative.

    const auto max_gas = std::numeric_limits<int64_t>::max();

    const auto code1 = bytecode{} + "0f";  // Block cost -1.
    execute(max_gas, code1);
    EXPECT_STATUS(EVMC_UNDEFINED_INSTRUCTION);

    const auto code2 = bytecode{} + OP_JUMPDEST + "c6" + "4b" + OP_STOP;  // Block cost -1.
    execute(max_gas, code2);
    EXPECT_STATUS(EVMC_UNDEFINED_INSTRUCTION);

    const auto code3 = bytecode{} + OP_ADDRESS + "2a" + "2b" + "2c" + "2d";  // Block cost -2.
    execute(max_gas, code3);
    EXPECT_STATUS(EVMC_UNDEFINED_INSTRUCTION);
}

TEST_F(evm, abort)
{
    for (auto r = 0; r <= EVMC_MAX_REVISION; ++r)
    {
        auto opcode = uint8_t{0xfe};
        auto res = vm.execute(host, evmc_revision(r), {}, &opcode, sizeof(opcode));
        EXPECT_EQ(res.status_code, EVMC_INVALID_INSTRUCTION);
    }
}

TEST_F(evm, staticmode)
{
    auto code_prefix = 1 + 6 * OP_DUP1;

    rev = EVMC_CONSTANTINOPLE;
    for (auto op : {OP_SSTORE, OP_LOG0, OP_LOG1, OP_LOG2, OP_LOG3, OP_LOG4, OP_CALL, OP_CREATE,
             OP_CREATE2, OP_SELFDESTRUCT})
    {
        msg.flags |= EVMC_STATIC;
        execute(code_prefix + hex(op));
        EXPECT_EQ(result.status_code, EVMC_STATIC_MODE_VIOLATION) << hex(op);
        EXPECT_EQ(result.gas_left, 0);
    }
}

TEST_F(evm, memory_big_allocation)
{
    constexpr auto size = 256 * 1024 + 1;
    const auto code = ret(0, size);
    execute(code);
    EXPECT_STATUS(EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, size);
    for (auto b : bytes_view{result.output_data, result.output_size})
        EXPECT_EQ(b, 0);
}

TEST_F(evm, memory_grow_mstore8)
{
    const auto code = calldataload(0) + push(0) + OP_JUMPDEST + mstore8(OP_DUP1, OP_DUP1) + add(1) +
                      jumpi(5, iszero(eq(OP_DUP3, OP_DUP1))) + ret(0, OP_MSIZE);

    const size_t size = 4 * 1024 + 256 + 1;
    auto input = std::ostringstream{};
    input << std::hex << std::setw(64) << std::setfill('0') << size;

    execute(code, input.str());
    EXPECT_STATUS(EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, ((size + 31) / 32) * 32);

    for (size_t i = 0; i < size; ++i)
        EXPECT_EQ(result.output_data[i], i % 256);

    for (size_t i = size; i < result.output_size; ++i)
        EXPECT_EQ(result.output_data[i], 0);
}

TEST_F(evm, mstore8_memory_cost)
{
    auto code = push(0) + mstore8(0);
    execute(12, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(11, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, sha3_memory_cost)
{
    execute(45, sha3(0, 1));
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(44, sha3(0, 1));
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, calldatacopy_memory_cost)
{
    auto code = push(1) + push(0) + push(0) + OP_CALLDATACOPY;
    execute(18, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(17, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm, max_code_size_push1)
{
    // TODO: Optimize the code generation.
    const auto code = (max_code_size / 2) * push(1);
    ASSERT_EQ(code.size(), max_code_size);

    execute(code);
    EXPECT_STATUS(EVMC_STACK_OVERFLOW);

    execute({code.data(), code.size() - 1});
    EXPECT_STATUS(EVMC_STACK_OVERFLOW);
}

TEST_F(evm, reverse_16_stack_items)
{
    // This test puts values 1, 2, ... , 16 on the stack and then reverse them with SWAP opcodes.
    // This uses all variants of SWAP instruction.

    constexpr auto n = 16;
    auto code = bytecode{};
    for (uint64_t i = 1; i <= n; ++i)
        code += push(i);
    code += push(0);                                        // Temporary stack item.
    code += bytecode{} + OP_SWAP16 + OP_SWAP1 + OP_SWAP16;  // Swap 1 and 16.
    code += bytecode{} + OP_SWAP15 + OP_SWAP2 + OP_SWAP15;  // Swap 2 and 15.
    code += bytecode{} + OP_SWAP14 + OP_SWAP3 + OP_SWAP14;
    code += bytecode{} + OP_SWAP13 + OP_SWAP4 + OP_SWAP13;
    code += bytecode{} + OP_SWAP12 + OP_SWAP5 + OP_SWAP12;
    code += bytecode{} + OP_SWAP11 + OP_SWAP6 + OP_SWAP11;
    code += bytecode{} + OP_SWAP10 + OP_SWAP7 + OP_SWAP10;
    code += bytecode{} + OP_SWAP9 + OP_SWAP8 + OP_SWAP9;
    code += bytecode{} + OP_POP;
    for (uint64_t i = 0; i < n; ++i)
        code += mstore8(i);
    code += ret(0, n);

    execute(code);

    EXPECT_STATUS(EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, n);
    EXPECT_EQ(hex({result.output_data, result.output_size}), "0102030405060708090a0b0c0d0e0f10");
}

struct memory_access_opcode
{
    evmc_opcode opcode;
    int memory_index_arg;
    int memory_size_arg;
};

struct memory_access_params
{
    uint64_t index;
    uint64_t size;
};

memory_access_opcode memory_access_opcodes[] = {
    {OP_SHA3, 0, 1},
    {OP_CALLDATACOPY, 0, 2},
    {OP_CODECOPY, 0, 2},
    {OP_MLOAD, 0, -1},
    {OP_MSTORE, 0, -1},
    {OP_MSTORE8, 0, -1},
    {OP_EXTCODECOPY, 1, 3},
    {OP_RETURNDATACOPY, 0, 2},
    {OP_LOG0, 0, 1},
    {OP_LOG1, 0, 1},
    {OP_LOG2, 0, 1},
    {OP_LOG3, 0, 1},
    {OP_LOG4, 0, 1},
    {OP_RETURN, 0, 1},
    {OP_REVERT, 0, 1},
    {OP_CALL, 3, 4},
    {OP_CALL, 5, 6},
    {OP_CALLCODE, 3, 4},
    {OP_CALLCODE, 5, 6},
    {OP_DELEGATECALL, 2, 3},
    {OP_DELEGATECALL, 4, 5},
    {OP_STATICCALL, 2, 3},
    {OP_STATICCALL, 4, 5},
    {OP_CREATE, 1, 2},
    {OP_CREATE2, 1, 2},
};

memory_access_params memory_access_test_cases[] = {
    {0, 0x100000000},
    {0x80000000, 0x80000000},
    {0x100000000, 0},
    {0x100000000, 1},
    {0x100000000, 0x100000000},
};

TEST_F(evm, memory_access)
{
    rev = EVMC_CONSTANTINOPLE;
    auto metrics = evmc_get_instruction_metrics_table(rev);
    auto names = evmc_get_instruction_names_table(rev);

    for (auto& p : memory_access_test_cases)
    {
        auto ss = std::ostringstream{};
        ss << std::hex << std::setw(10) << std::setfill('0') << p.size;
        const auto push_size = "64" + ss.str();
        ss.str({});
        ss << std::hex << std::setw(10) << std::setfill('0') << p.index;
        const auto push_index = "64" + ss.str();

        for (auto& t : memory_access_opcodes)
        {
            const int num_args = metrics[t.opcode].stack_height_required;
            auto h = std::max(num_args, t.memory_size_arg + 1);
            auto code = bytecode{};

            if (t.memory_size_arg >= 0)
            {
                while (--h != t.memory_size_arg)
                    code += push(0);

                code += push_size;
            }
            else if (p.index == 0 || p.size == 0)
                continue;  // Skip opcodes not having SIZE argument.

            while (--h != t.memory_index_arg)
                code += push(0);

            code += push_index;

            while (h-- != 0)
                code += push(0);

            code += bytecode{t.opcode};

            auto const gas = 8796294610952;
            execute(gas, code);

            auto case_descr_str = std::ostringstream{};
            case_descr_str << "offset = 0x" << std::hex << p.index << " size = 0x" << std::hex
                           << p.size << " opcode " << names[t.opcode];
            auto const case_descr = case_descr_str.str();

            if (p.size == 0)  // It is allowed to request 0 size memory at very big offset.
            {
                EXPECT_EQ(result.status_code, (t.opcode == OP_REVERT) ? EVMC_REVERT : EVMC_SUCCESS)
                    << case_descr;
                EXPECT_NE(result.gas_left, 0) << case_descr;
            }
            else
            {
                if (t.opcode == OP_RETURNDATACOPY)
                {
                    // In case of RETURNDATACOPY the "invalid memory access" might also be returned.
                    EXPECT_TRUE(result.status_code == EVMC_OUT_OF_GAS ||
                                result.status_code == EVMC_INVALID_MEMORY_ACCESS);
                }
                else
                {
                    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS) << case_descr;
                }

                EXPECT_EQ(result.gas_left, 0) << case_descr;
            }
        }
    }
}
