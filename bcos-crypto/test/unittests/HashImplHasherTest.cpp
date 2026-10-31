/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-crypto/hash/Sha3.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(HashImplHasherTest)

namespace
{
// Feed `input` through the streaming AnyHasher and return the digest bytes.
bcos::bytes streamDigest(hasher::AnyHasher anyHasher, const bcos::bytes& input)
{
    anyHasher.update(input);
    bcos::bytes out;
    anyHasher.final(out);
    return out;
}

const bcos::bytes c_input{0x61, 0x62, 0x63, 0x64, 0x65, 0x66};  // "abcdef"
}  // namespace

// For Keccak256, Sha3 and SM3 the streaming hasher() and the one-shot hash()
// use the same primitive, so they must produce identical digests.
BOOST_AUTO_TEST_CASE(hasherMatchesHashForConsistentImpls)
{
    {
        Keccak256 impl;
        BOOST_CHECK(streamDigest(impl.hasher(), c_input) == impl.hash(ref(c_input)).asBytes());
    }
    {
        class Sha3 impl;
        BOOST_CHECK(streamDigest(impl.hasher(), c_input) == impl.hash(ref(c_input)).asBytes());
    }
    {
        SM3 impl;
        BOOST_CHECK(streamDigest(impl.hasher(), c_input) == impl.hash(ref(c_input)).asBytes());
    }
}

// Sha256::hash() and Sha256::hasher() must both be SHA2-256 (issue #5356: hasher() used to
// return a SHA3-256 streaming hasher, copy-pasted from Sha3).
BOOST_AUTO_TEST_CASE(sha256HasherMatchesHash)
{
    Sha256 sha256;
    auto oneShot = sha256.hash(ref(c_input)).asBytes();
    auto streaming = streamDigest(sha256.hasher(), c_input);
    BOOST_CHECK(oneShot == streaming);

    // and it is SHA2-256, not SHA3-256: differs from Sha3 for the same input
    class Sha3 sha3;
    BOOST_CHECK(streaming != sha3.hash(ref(c_input)).asBytes());

    // SHA2-256("abcdef") reference vector
    BOOST_CHECK_EQUAL(sha256.hash(ref(c_input)).hex(),
        "bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721");
}

// getHashImplType() distinguishes Sha256 from Sha3 (issue #5356: Sha256's ctor used to set Sha3).
BOOST_AUTO_TEST_CASE(sha256ReportsOwnImplType)
{
    Sha256 sha256;
    BOOST_CHECK(sha256.getHashImplType() == HashImplType::Sha256Hash);

    class Sha3 sha3;
    BOOST_CHECK(sha3.getHashImplType() == HashImplType::Sha3);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
