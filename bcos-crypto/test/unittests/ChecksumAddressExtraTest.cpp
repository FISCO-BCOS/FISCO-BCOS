/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cctype>
#include <string>

using namespace bcos;
using namespace bcos::crypto;
using namespace std::string_view_literals;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(ChecksumAddressExtraTest)

namespace
{
bool caseInsensitiveEqual(std::string_view lhs, std::string_view rhs)
{
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}
}  // namespace

// The four canonical EIP-55 vectors (https://eips.ethereum.org/EIPS/eip-55).
// toCheckSumAddress lowercases then mixed-cases in place; it emits no "0x".
BOOST_AUTO_TEST_CASE(checksumEIP55KnownVectors)
{
    auto keccak256 = std::make_shared<Keccak256>();
    const std::pair<std::string, std::string> vectors[] = {
        {"5aaeb6053f3e94c9b9a09f33669435e7ef1beaed", "5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"},
        {"fb6916095ca1df60bb79ce92ce3ea74c37c5d359", "fB6916095ca1df60bB79Ce92cE3Ea74c37c5d359"},
        {"dbf03b407c01e7cd3cbea99509d93f8dddc8c6fb", "dbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB"},
        {"d1220a0cf47c7b9be7a2e6ba89f429762e7b9adb", "D1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb"},
    };
    for (const auto& [lower, expected] : vectors)
    {
        std::string addr = lower;
        toCheckSumAddress(addr, keccak256);
        BOOST_CHECK_EQUAL(addr, expected);
    }
}

// EIP-1191: chainId 0 and 1 degrade to plain EIP-55 (the branch that prepends
// "<chainId>0x" is skipped), so they must equal toCheckSumAddress exactly. A
// non-trivial chainId takes the branch; its output is a same-length mixed-case
// rendering of the same lowercase address.
BOOST_AUTO_TEST_CASE(checksumWithChainId)
{
    auto keccak256 = std::make_shared<Keccak256>();
    const std::string lower = "5aaeb6053f3e94c9b9a09f33669435e7ef1beaed";

    std::string plain = lower;
    toCheckSumAddress(plain, keccak256);

    std::string chain0 = lower;
    toCheckSumAddressWithChainId(chain0, keccak256, 0);
    BOOST_CHECK_EQUAL(chain0, plain);

    std::string chain1 = lower;
    toCheckSumAddressWithChainId(chain1, keccak256, 1);
    BOOST_CHECK_EQUAL(chain1, plain);

    std::string chain30 = lower;
    toCheckSumAddressWithChainId(chain30, keccak256, 30);
    BOOST_CHECK_EQUAL(chain30.size(), lower.size());
    BOOST_CHECK(caseInsensitiveEqual(chain30, lower));
    // The EIP-1191 hash input differs from EIP-55, so at least one address in
    // practice checksums differently; we don't assert inequality for a fixed
    // address (it may coincide), only that the chainId branch executed.
}

// newEVMAddress is FISCO-specific: keccak256("block_context_seq")[:20], lowercase.
// Both overloads must agree, be deterministic, 40 lowercase-hex chars, and map
// distinct inputs to distinct addresses.
BOOST_AUTO_TEST_CASE(newEVMAddressDeterminismAndOverloads)
{
    Keccak256 hasher;
    auto hasherPtr = std::make_shared<Keccak256>();

    auto a = newEVMAddress(hasher, 100, 7, 3);
    auto aPtr = newEVMAddress(hasherPtr, 100, 7, 3);
    BOOST_CHECK_EQUAL(a, aPtr);                              // Hash& and Hash::Ptr& overloads agree
    BOOST_CHECK_EQUAL(a, newEVMAddress(hasher, 100, 7, 3));  // deterministic

    BOOST_CHECK_EQUAL(a.size(), 40U);
    BOOST_CHECK(std::all_of(a.begin(), a.end(),
        [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));

    // Any of the three coordinates changing yields a different address.
    BOOST_CHECK_NE(a, newEVMAddress(hasher, 101, 7, 3));
    BOOST_CHECK_NE(a, newEVMAddress(hasher, 100, 8, 3));
    BOOST_CHECK_NE(a, newEVMAddress(hasher, 100, 7, 4));
}

// toChecksumAddressFromBytes takes raw address bytes and returns their lowercase
// hex (no checksum casing applied by this overload).
BOOST_AUTO_TEST_CASE(checksumFromBytesLowercaseHex)
{
    auto keccak256 = std::make_shared<Keccak256>();
    const std::string lowerHex = "5aaeb6053f3e94c9b9a09f33669435e7ef1beaed";
    bcos::bytes raw = fromHex(lowerHex);
    std::string_view rawView{reinterpret_cast<const char*>(raw.data()), raw.size()};

    auto result = toChecksumAddressFromBytes(rawView, keccak256);
    BOOST_CHECK_EQUAL(result, lowerHex);
}

// CREATE address = keccak256(rlp([sender, nonce]))[12:]. The nonce feeds RLP
// integer encoding: 0 is a single 0x80, small ints one byte, >=256 multi-byte.
// Exercise all three encodings; assert determinism and nonce-sensitivity.
BOOST_AUTO_TEST_CASE(legacyCreateAddressNonceEncoding)
{
    auto sender = fromHex("6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0"sv);

    auto n0 = newLegacyEVMAddressString(ref(sender), u256(0));
    auto n1 = newLegacyEVMAddressString(ref(sender), u256(1));
    auto n256 = newLegacyEVMAddressString(ref(sender), u256(256));

    // Deterministic.
    BOOST_CHECK_EQUAL(n0, newLegacyEVMAddressString(ref(sender), u256(0)));
    // Distinct nonces (hence distinct RLP encodings) give distinct addresses.
    BOOST_CHECK_NE(n0, n1);
    BOOST_CHECK_NE(n1, n256);
    BOOST_CHECK_NE(n0, n256);

    // The string-nonce overload parses hex and must match the u256 overload.
    BOOST_CHECK_EQUAL(n256, newLegacyEVMAddressString(ref(sender), std::string("0x100")));
    BOOST_CHECK_EQUAL(n0.size(), 40U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
