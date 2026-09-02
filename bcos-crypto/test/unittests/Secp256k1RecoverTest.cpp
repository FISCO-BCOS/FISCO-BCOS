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
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(Secp256k1RecoverTest)

// The ecrecover-style helper: a 128-byte hash||v||r||s buffer. Bad inputs must
// return {false, {}} rather than throwing or producing an address.
BOOST_AUTO_TEST_CASE(recoverFromInputRejectsBad)
{
    auto hashImpl = std::make_shared<Keccak256>();

    // v outside the accepted 27..28 range: rejected before any recovery.
    bcos::bytes input(128, 0);
    auto [okZero, addrZero] = secp256k1Recover(hashImpl, bcos::ref(input));
    BOOST_CHECK(!okZero);
    BOOST_CHECK(addrZero.empty());

    // v == 27 but zero r/s: recovery fails internally and is caught.
    bcos::bytes withV(128, 0);
    withV[63] = 27;  // low byte of the big-endian h256 v field
    auto [okBadSig, addrBadSig] = secp256k1Recover(hashImpl, bcos::ref(withV));
    BOOST_CHECK(!okBadSig);
}

// recoverAddress rejects a wrong-length signature (checkSigLen) and a
// correctly-sized but unrecoverable signature (secp256k1Recover failure).
BOOST_AUTO_TEST_CASE(recoverAddressRejectsBadSignature)
{
    Secp256k1Crypto crypto;
    Keccak256 hasher;
    HashType hash;  // zero hash

    bcos::bytes shortSig(10, 0);
    BOOST_CHECK_THROW(crypto.recoverAddress(hasher, hash, bcos::ref(shortSig)), std::exception);

    bcos::bytes badSig(SECP256K1_SIGNATURE_LEN, 0);
    BOOST_CHECK_THROW(crypto.recoverAddress(hasher, hash, bcos::ref(badSig)), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
