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
#include <boost/test/unit_test.hpp>

using namespace bcos::cppsdk::utilities;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(KeyPairBuilderTest)

BOOST_AUTO_TEST_CASE(genSecp256K1KeyPairFromRandom)
{
    KeyPairBuilder builder;
    auto kp = builder.genKeyPair(CryptoType::Secp256K1);
    BOOST_REQUIRE(kp);
    BOOST_CHECK(!kp->secretKey()->data().empty());
    BOOST_CHECK(!kp->publicKey()->data().empty());
}

BOOST_AUTO_TEST_CASE(genSecp256K1KeyPairFromExplicitPrivateKeyIsDeterministic)
{
    KeyPairBuilder builder;
    // 32-byte private key in raw bytes (the path takes bytesConstRef).
    bytes priv(32, 0x42);
    auto first = builder.genKeyPair(CryptoType::Secp256K1, bytesConstRef(priv.data(), priv.size()));
    auto second =
        builder.genKeyPair(CryptoType::Secp256K1, bytesConstRef(priv.data(), priv.size()));
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    BOOST_CHECK(first->publicKey()->data() == second->publicKey()->data());
}

BOOST_AUTO_TEST_CASE(genSm2KeyPairFromRandom)
{
    KeyPairBuilder builder;
    auto kp = builder.genKeyPair(CryptoType::SM2);
    BOOST_REQUIRE(kp);
    BOOST_CHECK(!kp->secretKey()->data().empty());
    BOOST_CHECK(!kp->publicKey()->data().empty());
}

BOOST_AUTO_TEST_CASE(genSm2KeyPairFromExplicitPrivateKeyIsDeterministic)
{
    KeyPairBuilder builder;
    bytes priv(32, 0x37);
    auto first = builder.genKeyPair(CryptoType::SM2, bytesConstRef(priv.data(), priv.size()));
    auto second = builder.genKeyPair(CryptoType::SM2, bytesConstRef(priv.data(), priv.size()));
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    BOOST_CHECK(first->publicKey()->data() == second->publicKey()->data());
}

BOOST_AUTO_TEST_CASE(genKeyPairUnsupportedAlgorithmThrows)
{
    KeyPairBuilder builder;
    // Cast a value outside the enum to hit the default branch.
    auto bogus = static_cast<CryptoType>(99);
    BOOST_CHECK_THROW(builder.genKeyPair(bogus), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
