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

// BUG (recorded, not fixed per task scope): Sha256::hash() computes SHA2-256
// (OpenSSL_SHA2_256_Hasher) but Sha256::hasher() returns a SHA3-256 streaming
// hasher (OpenSSL_SHA3_256_Hasher) -- a copy-paste from Sha3 in HashImpl.cpp.
// The two therefore disagree, and Sha256::hasher() actually matches Sha3.
// This is a pinning test of the current behavior; when HashImpl.cpp is fixed
// so hasher() returns OpenSSL_SHA2_256_Hasher, this will fail and should be
// changed to assert equality.
BOOST_AUTO_TEST_CASE(sha256HasherInconsistentWithHash)
{
    Sha256 sha256;
    auto oneShot = sha256.hash(ref(c_input)).asBytes();       // SHA2-256
    auto streaming = streamDigest(sha256.hasher(), c_input);  // SHA3-256 (bug)
    BOOST_CHECK(oneShot != streaming);

    // Sha256::hasher() is really a SHA3-256 hasher, so it matches Sha3.
    class Sha3 sha3;
    BOOST_CHECK(streaming == streamDigest(sha3.hasher(), c_input));
    BOOST_CHECK(streaming == sha3.hash(ref(c_input)).asBytes());
}

// BUG (recorded): there is no Sha256 value in HashImplType; Sha256's ctor sets
// the impl type to Sha3 (copy-paste from Sha3), so getHashImplType() cannot
// distinguish a Sha256 from a Sha3 and mislabels the SHA2-256 one-shot path.
BOOST_AUTO_TEST_CASE(sha256ReportsSha3ImplType)
{
    Sha256 sha256;
    BOOST_CHECK(sha256.getHashImplType() == HashImplType::Sha3);

    class Sha3 sha3;
    BOOST_CHECK(sha3.getHashImplType() == HashImplType::Sha3);  // indistinguishable
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
