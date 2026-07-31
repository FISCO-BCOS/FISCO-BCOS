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
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/signature/ed25519/Ed25519Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(SignatureRoundtripTest)

// Full ed25519 path: sign -> verify via the shared_ptr<bytes const> overload
// -> createKeyPair reproduces the key -> ed25519Recover(hashImpl, input)
// success path yields the signer's address. The recover input buffer is
// hash || pub || r || s; sign(withPub=true) returns r||s||pub, so r||s is the
// first 64 bytes.
BOOST_AUTO_TEST_CASE(ed25519SignVerifyRecoverRoundtrip)
{
    Ed25519Crypto crypto;
    auto hashImpl = std::make_shared<Keccak256>();
    auto keyPair = crypto.generateKeyPair();
    auto hash = keccak256Hash(bytesConstRef(std::string("ed25519-roundtrip")));

    auto sig = crypto.sign(*keyPair, hash, true);
    BOOST_REQUIRE_EQUAL(sig->size(), 96U);

    // verify(shared_ptr<bytes const>, ...) overload
    auto pubBytes = std::make_shared<bytes>(keyPair->publicKey()->data());
    BOOST_CHECK(crypto.verify(pubBytes, hash, ref(*sig)));

    // createKeyPair(secret) reproduces the same public key
    auto reproduced = crypto.createKeyPair(keyPair->secretKey());
    BOOST_CHECK(reproduced->publicKey()->data() == keyPair->publicKey()->data());

    // recover success path
    const auto& pub = keyPair->publicKey()->data();
    bcos::bytes input(hash.begin(), hash.end());
    input.insert(input.end(), pub.begin(), pub.end());
    input.insert(input.end(), sig->begin(), sig->begin() + 64);  // r||s
    BOOST_REQUIRE_EQUAL(input.size(), 128U);

    auto [ok, addr] = ed25519Recover(hashImpl, ref(input));
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(addr.size(), 20U);
    // Deterministic: the same input recovers the same 20-byte address.
    auto [ok2, addr2] = ed25519Recover(hashImpl, ref(input));
    BOOST_CHECK(ok2);
    BOOST_CHECK(addr == addr2);
}

// SM2 sign -> verify via the shared_ptr<bytes const> overload -> createKeyPair
// reproduces the key.
BOOST_AUTO_TEST_CASE(sm2SignVerifyAndCreateKeyPair)
{
    SM2Crypto crypto;
    auto keyPair = crypto.generateKeyPair();
    auto hash = keccak256Hash(bytesConstRef(std::string("sm2-roundtrip")));

    auto sig = crypto.sign(*keyPair, hash, true);
    BOOST_REQUIRE_EQUAL(sig->size(), 128U);

    auto pubBytes = std::make_shared<bytes>(keyPair->publicKey()->data());
    BOOST_CHECK(crypto.verify(pubBytes, hash, ref(*sig)));

    auto reproduced = crypto.createKeyPair(keyPair->secretKey());
    BOOST_CHECK(reproduced->publicKey()->data() == keyPair->publicKey()->data());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
