// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <gtest/gtest.h>
#include <test/utils/bytecode.hpp>

TEST(bytecode, push)
{
    auto code = push("0102") + OP_POP + push("010203040506070809") + "50";
    EXPECT_EQ(hex(code), "610102506801020304050607080950");

    EXPECT_THROW(push(""), std::invalid_argument);
    auto data = bytes(33, '\x00');
    EXPECT_THROW(push(data), std::invalid_argument);
}

TEST(bytecode, push_int)
{
    EXPECT_EQ(push(0), "6000");
    EXPECT_EQ(push(0xff), "60ff");
    EXPECT_EQ(push(0x100), "610100");
    EXPECT_EQ(push(0x112233), "62112233");
    EXPECT_EQ(push(0xf22334455667788), "670f22334455667788");
    EXPECT_EQ(push(0x1122334455667788), "671122334455667788");
    EXPECT_EQ(push(0xffffffffffffffff), "67ffffffffffffffff");
}

TEST(bytecode, add)
{
    auto e = "6007600d0160005260206000f3";
    auto code = bytecode{} + 7 + 0xd + OP_ADD + 0 + "52" + 0x20 + 0x00 + "f3";
    EXPECT_EQ(code, e);
    code = add(13, 7) + 0 + OP_MSTORE + 0x20 + 0 + OP_RETURN;
    EXPECT_EQ(code, e);
    code = add(13, 7) + mstore(0) + ret(0, 0x20);
    EXPECT_EQ(code, e);
    code = add(13, 7) + ret_top();
    EXPECT_EQ(code, e);
    code = ret(add(13, 7));
    EXPECT_EQ(code, e);
}

TEST(bytecode, repeat)
{
    auto code = 0 + 2 * OP_DUP1 + "3c";
    EXPECT_EQ(code, "600080803c");

    EXPECT_EQ(0 * OP_STOP, "");
}

TEST(bytecode, to_name)
{
    EXPECT_EQ(to_name(OP_SAR), "SAR");
    EXPECT_EQ(to_name(OP_SAR, EVMC_HOMESTEAD), "UNDEFINED_INSTRUCTION:1d");
}

TEST(bytecode, decode)
{
    const auto code = push(0x01e240) + OP_DUP1 + OP_GAS + "cc" + OP_REVERT;
    EXPECT_EQ(decode(code, EVMC_FRONTIER),
        "bytecode{} + OP_PUSH3 + \"01e240\" + OP_DUP1 + OP_GAS + \"cc\" + \"fd\"");
    EXPECT_EQ(decode(code, EVMC_BYZANTIUM),
        "bytecode{} + OP_PUSH3 + \"01e240\" + OP_DUP1 + OP_GAS + \"cc\" + OP_REVERT");
}

TEST(bytecode, decode_push_trimmed_data)
{
    const auto code1 = bytecode{} + OP_PUSH2 + "0000";
    EXPECT_EQ(decode(code1, EVMC_FRONTIER), "bytecode{} + OP_PUSH2 + \"0000\"");

    const auto code2 = bytecode{} + OP_PUSH2 + "00";
    EXPECT_EQ(decode(code2, EVMC_FRONTIER), "bytecode{} + OP_PUSH2 + \"00\"");

    const auto code3 = bytecode{} + OP_PUSH2;
    EXPECT_EQ(decode(code3, EVMC_FRONTIER), "bytecode{} + OP_PUSH2");

    const auto code4 = bytecode{} + OP_PUSH2 + "";
    EXPECT_EQ(decode(code4, EVMC_FRONTIER), "bytecode{} + OP_PUSH2");
}
