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
#include <bcos-crypto/signature/ed25519/Ed25519Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(Ed25519RecoverTest)

// The ecrecover-style helper takes a 128-byte hash||pub||r||s buffer. An
// all-zero (invalid) buffer must fail verification and return {false, {}}
// instead of throwing or yielding an address.
BOOST_AUTO_TEST_CASE(recoverFromInputRejectsInvalid)
{
    auto hashImpl = std::make_shared<Keccak256>();
    bcos::bytes input(128, 0);
    auto [ok, addr] = ed25519Recover(hashImpl, bcos::ref(input));
    BOOST_CHECK(!ok);
    BOOST_CHECK(addr.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
