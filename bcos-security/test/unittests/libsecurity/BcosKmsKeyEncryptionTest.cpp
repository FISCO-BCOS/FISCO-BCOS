/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-security/BcosKmsKeyEncryption.h>
#include <bcos-security/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::security;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(BcosKmsKeyEncryptionTest)

BOOST_AUTO_TEST_CASE(directConstructorAesPath)
{
    BcosKmsKeyEncryption enc("aes-data-key-32-bytes-xxxxxxxxxx", /*smCryptoType=*/false);
    enc.setCompatibilityVersion(42);
    BOOST_CHECK_EQUAL(enc.compatibilityVersion(), 42U);
}

BOOST_AUTO_TEST_CASE(directConstructorSmPath)
{
    BcosKmsKeyEncryption enc("sm-data-key-32-bytes-xxxxxxxxxxx", /*smCryptoType=*/true);
    enc.setCompatibilityVersion(7);
    BOOST_CHECK_EQUAL(enc.compatibilityVersion(), 7U);
}

BOOST_AUTO_TEST_CASE(encryptContentsAlwaysThrowsNotImplemented)
{
    BcosKmsKeyEncryption enc("any", false);
    auto payload = std::make_shared<bytes>(bytes{1, 2, 3});
    BOOST_CHECK_THROW(enc.encryptContents(payload), NotImplementedError);
}

BOOST_AUTO_TEST_CASE(encryptFileAlwaysThrowsNotImplemented)
{
    BcosKmsKeyEncryption enc("any", false);
    BOOST_CHECK_THROW(enc.encryptFile("/no/such/file"), NotImplementedError);
}

BOOST_AUTO_TEST_CASE(decryptContentsOnGarbageWrapsAsEncryptedFileError)
{
    // fromHex on non-hex input throws inside decryptContents; the catch must
    // wrap it as EncryptedFileError rather than leak the raw exception type.
    BcosKmsKeyEncryption enc("any-key", false);
    auto garbage = std::make_shared<bytes>(bytes{'n', 'o', 't', '-', 'h', 'e', 'x'});
    BOOST_CHECK_THROW(enc.decryptContents(garbage), EncryptedFileError);
}

BOOST_AUTO_TEST_CASE(decryptFileMissingPathWrapsAsEncryptedFileError)
{
    BcosKmsKeyEncryption enc("any-key", false);
    BOOST_CHECK_THROW(enc.decryptFile("/nonexistent/path/key.file"), EncryptedFileError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
