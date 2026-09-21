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
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(Secp256k1RecidGuardTest)

// Regression for the recid range guard (issue #5588). A signature whose trailing recid
// byte is outside 0..3 must be rejected as InvalidSignature by every secp256k1 entry
// point. Before the guard, recoverAddress passed the byte straight into
// secp256k1_ecdsa_recoverable_signature_parse_compact, whose ARG_CHECK aborts the
// process. On an unguarded build this case dies with SIGABRT (Boost.Test reports it as
// a fatal signal) instead of failing an assertion.
BOOST_AUTO_TEST_CASE(outOfRangeRecidIsRejectedNotAborted)
{
    Secp256k1Crypto crypto;
    auto hasher = std::make_shared<Keccak256>();
    auto keyPair = crypto.generateKeyPair();
    auto hash = hasher->hash(bytesConstRef((const byte*)"recid guard", 11));

    auto signature = crypto.sign(*keyPair, hash, true);
    BOOST_REQUIRE_EQUAL(signature->size(), SECP256K1_SIGNATURE_LEN);

    // Sanity: the untouched signature recovers the signer.
    auto [okValid, addr] = crypto.recoverAddress(*hasher, hash, ref(*signature));
    BOOST_CHECK(okValid);
    BOOST_CHECK_EQUAL(toHex(addr), toHex(keyPair->address(hasher).asBytes()));
    BOOST_CHECK(secp256k1Verify(keyPair->publicKey(), hash, ref(*signature)));

    // Same r/s, only the recid byte tampered: the guard is the only thing standing
    // between this input and libsecp256k1's abort().
    for (uint8_t badRecid : {uint8_t{4}, uint8_t{27}, uint8_t{0xff}})
    {
        bytes tampered = *signature;
        tampered[SECP256K1_SIGNATURE_V] = badRecid;
        BOOST_CHECK_THROW(crypto.recoverAddress(*hasher, hash, ref(tampered)), InvalidSignature);
        BOOST_CHECK_THROW(secp256k1Recover(hash, ref(tampered)), InvalidSignature);
        BOOST_CHECK_THROW(
            secp256k1Verify(keyPair->publicKey(), hash, ref(tampered)), InvalidSignature);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
