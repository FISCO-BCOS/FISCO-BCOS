// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <gtest/gtest.h>
#include <test/utils/utils.hpp>

TEST(utils, hex)
{
    auto data = bytes{0x0, 0x1, 0xa, 0xf, 0x1f, 0xa0, 0xff, 0xf0};
    EXPECT_EQ(hex(data), "00010a0f1fa0fff0");
}

TEST(utils, from_hex_empty)
{
    EXPECT_TRUE(from_hex({}).empty());
}

TEST(utils, from_hex_odd_input_length)
{
    EXPECT_THROW(from_hex("0"), std::length_error);
}

TEST(utils, from_hex_capital_letters)
{
    EXPECT_EQ(from_hex("ABCDEF"), (bytes{0xab, 0xcd, 0xef}));
}

TEST(utils, from_hex_invalid_encoding)
{
    EXPECT_THROW(from_hex({"\0\0", 2}), std::out_of_range);
}

TEST(utils, hex_byte)
{
    auto b = uint8_t{};
    EXPECT_EQ(hex(b), "00");
    b = 0xaa;
    EXPECT_EQ(hex(b), "aa");
}

TEST(utils, from_hexx)
{
    EXPECT_EQ(hex(from_hexx("")), "");

    EXPECT_EQ(hex(from_hexx("(0xca)")), "");
    EXPECT_EQ(hex(from_hexx("(1xca)")), "ca");
    EXPECT_EQ(hex(from_hexx("(5xca)")), "cacacacaca");

    EXPECT_EQ(hex(from_hexx("01(0x3a)02")), "0102");
    EXPECT_EQ(hex(from_hexx("01(1x3a)02")), "013a02");
    EXPECT_EQ(hex(from_hexx("01(2x3a)02")), "013a3a02");

    EXPECT_EQ(hex(from_hexx("01(2x333)02(2x4444)03")), "01333333024444444403");
    EXPECT_EQ(hex(from_hexx("01(4x333)02(4x4)03")), "0133333333333302444403");

    EXPECT_EQ(hex(from_hexx("00")), "00");
}
