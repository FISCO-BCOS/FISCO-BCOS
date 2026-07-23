/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Seam test: KECCAK256 opcode routes through evmone::VM::hash_fn when set,
 *         falls back to upstream ethash::keccak256 when not.
 *  @file HashFnRoutingTest.cpp
 */
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <evmone/vm.hpp>

namespace bcos::test
{

namespace
{
// PUSH1 0, PUSH1 0, KECCAK256, PUSH1 0, MSTORE, PUSH1 32, PUSH1 0, RETURN
// => returns the 32-byte hash of the empty input.
constexpr uint8_t kKeccakEmptyBytecode[] = {
    0x60, 0x00, 0x60, 0x00, 0x20, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};

// keccak256("") — well-known constant.
constexpr uint8_t kKeccakEmptyDigest[32] = {0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c, 0x92,
    0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0, 0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b, 0x7b,
    0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70};

evmc_bytes32 sentinelHash(evmc_host_context* /*context*/, const uint8_t* /*data*/, size_t /*size*/)
{
    evmc_bytes32 h{};
    for (size_t i = 0; i < sizeof(h.bytes); ++i)
    {
        h.bytes[i] = static_cast<uint8_t>(i + 1);
    }
    return h;
}

// No host callback fires for this bytecode; a zeroed interface is sufficient.
const evmc_host_interface kEmptyHostInterface{};

evmc::Result runKeccakEmpty(bool withSentinel)
{
    evmc::VM vm{evmc_create_evmone()};
    if (withSentinel)
    {
        static_cast<evmone::VM*>(vm.get_raw_pointer())->hash_fn = sentinelHash;
    }
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100000;
    return vm.execute(kEmptyHostInterface, nullptr, EVMC_SHANGHAI, msg, kKeccakEmptyBytecode,
        sizeof(kKeccakEmptyBytecode));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(HashFnRoutingTest)

BOOST_AUTO_TEST_CASE(keccak256OpcodeUsesVmHashFnWhenSet)
{
    auto result = runKeccakEmpty(true);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result.output_size, 32U);
    for (size_t i = 0; i < 32; ++i)
    {
        BOOST_CHECK_EQUAL(result.output_data[i], static_cast<uint8_t>(i + 1));
    }
}

BOOST_AUTO_TEST_CASE(keccak256OpcodeFallsBackToKeccakWhenUnset)
{
    auto result = runKeccakEmpty(false);
    BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(result.output_size, 32U);
    BOOST_CHECK_EQUAL(std::memcmp(result.output_data, kKeccakEmptyDigest, 32), 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
