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
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(SM2RecoverTest)

// recoverAddress consumes a 160-byte hash||pub||r||s buffer. An all-zero
// (invalid) buffer must fail SM2 verification and return {false, {}} rather
// than throwing or producing an address.
BOOST_AUTO_TEST_CASE(recoverAddressFromInputRejectsInvalid)
{
    SM2Crypto crypto;
    auto hashImpl = std::make_shared<Keccak256>();
    bcos::bytes input(160, 0);
    auto [ok, addr] = crypto.recoverAddress(hashImpl, bcos::ref(input));
    BOOST_CHECK(!ok);
    BOOST_CHECK(addr.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
