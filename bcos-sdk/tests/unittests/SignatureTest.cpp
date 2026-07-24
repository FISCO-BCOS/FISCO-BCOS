/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-cpp-sdk/utilities/crypto/KeyPairBuilder.h>
#include <bcos-cpp-sdk/utilities/crypto/Signature.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::cppsdk::utilities;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(SdkSignatureTest)

// Every Signature method only implements the HsmSM2 branch (which needs an HSM
// shared library); any other algorithm hits the else branch and throws. These
// tests exercise the dispatch + the unsupported-algorithm throw path, which is
// all that's reachable without HSM hardware.

BOOST_AUTO_TEST_CASE(signRejectsNonHsmKeyPair)
{
    KeyPairBuilder builder;
    auto keyPair = builder.genKeyPair(CryptoType::Secp256K1);  // keyPairType() != HsmSM2
    Signature sig;
    bcos::crypto::HashType hash;
    BOOST_CHECK_THROW(sig.sign(*keyPair, hash, ""), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(verifyRejectsNonHsmType)
{
    Signature sig;
    bcos::crypto::HashType hash;
    auto pub = std::make_shared<bcos::bytes const>();
    bcos::bytes signature;
    BOOST_CHECK_THROW(
        sig.verify(CryptoType::Secp256K1, pub, hash, bcos::ref(signature), ""), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(recoverRejectsNonHsmType)
{
    Signature sig;
    bcos::crypto::HashType hash;
    bcos::bytes signature;
    BOOST_CHECK_THROW(
        sig.recover(CryptoType::SM2, hash, bcos::ref(signature), ""), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(recoverAddressRejectsNonHsmType)
{
    Signature sig;
    bcos::bytes in;
    BOOST_CHECK_THROW(
        sig.recoverAddress(CryptoType::Ed25519, nullptr, bcos::ref(in), ""), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
