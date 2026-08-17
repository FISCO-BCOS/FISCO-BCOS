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
#include <bcos-security/BcosKms.h>
#include <bcos-security/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::security;

namespace bcos::test
{
namespace
{
// Mock that returns a canned response from callMethod, no network involved.
class MockKmsClient : public BcosKmsHttpClientInterface
{
public:
    int connectCalls = 0;
    int closeCalls = 0;
    std::string lastMethod;
    Json::Value lastParams;
    Json::Value response;
    bool throwOnCall = false;

    void connect() override { ++connectCalls; }
    void close() override { ++closeCalls; }
    Json::Value callMethod(const std::string& method, Json::Value params) override
    {
        lastMethod = method;
        lastParams = params;
        if (throwOnCall)
        {
            throw std::runtime_error("simulated transport failure");
        }
        return response;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(BcosKmsTest)

BOOST_AUTO_TEST_CASE(setIpPortStoresUrlComposition)
{
    BcosKms kms;
    kms.setIpPort("127.0.0.1", 5678);
    BOOST_CHECK_EQUAL(kms.url(), "127.0.0.1:5678");
}

BOOST_AUTO_TEST_CASE(uniformDataKeyKeccakBranch)
{
    BcosKms kms;
    bytes input{0xDE, 0xAD, 0xBE, 0xEF};
    bytes out = kms.uniformDataKey(input, /*isSMCrypto=*/false);
    // Keccak path: result is a single 32-byte keccak256 of the input.
    auto expected = crypto::keccak256Hash(ref(input)).asBytes();
    BOOST_REQUIRE_EQUAL(out.size(), expected.size());
    BOOST_CHECK(out == expected);
}

BOOST_AUTO_TEST_CASE(uniformDataKeySmBranchQuadruplesHash)
{
    BcosKms kms;
    bytes input{0x01, 0x02, 0x03};
    bytes out = kms.uniformDataKey(input, /*isSMCrypto=*/true);
    // SM path: hash repeated four times.
    auto oneTurn = crypto::sm3Hash(ref(input)).asBytes();
    BOOST_REQUIRE_EQUAL(out.size(), oneTurn.size() * 4U);
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < oneTurn.size(); ++j)
        {
            BOOST_CHECK_EQUAL(out[i * oneTurn.size() + j], oneTurn[j]);
        }
    }
}

BOOST_AUTO_TEST_CASE(getDataKeyEmptyCipherThrows)
{
    BcosKms kms;
    BOOST_CHECK_THROW(kms.getDataKey("", false), KeyCenterDataKeyError);
}

BOOST_AUTO_TEST_CASE(getDataKeyHappyPathUsingMockClient)
{
    auto mock = std::make_shared<MockKmsClient>();
    bytes realKey{0xAA, 0xBB, 0xCC, 0xDD};
    Json::Value rsp;
    rsp["error"] = 0;
    rsp["dataKey"] = toHex(realKey);
    rsp["info"] = "";
    mock->response = rsp;

    BcosKms kms;
    kms.setKcClient(mock);
    auto out = kms.getDataKey("cipher-text", /*isSMCrypto=*/false);

    BOOST_CHECK_EQUAL(mock->connectCalls, 1);
    BOOST_CHECK_EQUAL(mock->closeCalls, 1);
    BOOST_CHECK_EQUAL(mock->lastMethod, "decDataKey");
    // Output is the keccak256 of the decoded key.
    auto expected = crypto::keccak256Hash(ref(realKey)).asBytes();
    BOOST_CHECK(out == expected);
}

BOOST_AUTO_TEST_CASE(getDataKeyCacheHitSkipsCallMethod)
{
    auto mock = std::make_shared<MockKmsClient>();
    bytes realKey{0x11, 0x22};
    Json::Value rsp;
    rsp["error"] = 0;
    rsp["dataKey"] = toHex(realKey);
    rsp["info"] = "";
    mock->response = rsp;

    BcosKms kms;
    kms.setKcClient(mock);
    auto first = kms.getDataKey("same-cipher", false);
    auto second = kms.getDataKey("same-cipher", false);  // cache hit: no more network

    BOOST_CHECK(first == second);
    BOOST_CHECK_EQUAL(mock->connectCalls, 1);  // only first call connects
}

BOOST_AUTO_TEST_CASE(getDataKeyServerErrorClearsCacheAndThrows)
{
    auto mock = std::make_shared<MockKmsClient>();
    bytes realKey{0x11, 0x22};
    Json::Value ok;
    ok["error"] = 0;
    ok["dataKey"] = toHex(realKey);
    ok["info"] = "";
    mock->response = ok;

    BcosKms kms;
    kms.setKcClient(mock);

    // Seed the single-entry cache with a successful fetch for c1, and confirm a
    // repeat is a cache hit (no new connect).
    kms.getDataKey("c1", false);
    kms.getDataKey("c1", false);
    BOOST_CHECK_EQUAL(mock->connectCalls, 1);

    // A server-error response for a different cipher goes through the catch,
    // which calls clearCache() before rethrowing.
    Json::Value err;
    err["error"] = 1;
    err["dataKey"] = "00";
    err["info"] = "kms unavailable";
    mock->response = err;
    BOOST_CHECK_THROW(kms.getDataKey("c2", false), KeyCenterConnectionError);  // connect #2

    // The previously cached c1 must now be cleared: the next c1 fetch re-connects
    // (connect #3) instead of returning the stale cached value. Without a working
    // clearCache() this would be a cache hit and connectCalls would stay at 2.
    mock->response = ok;
    kms.getDataKey("c1", false);
    BOOST_CHECK_EQUAL(mock->connectCalls, 3);
}

BOOST_AUTO_TEST_CASE(getDataKeyTransportThrowWrapsAsKeyCenterError)
{
    auto mock = std::make_shared<MockKmsClient>();
    mock->throwOnCall = true;
    BcosKms kms;
    kms.setKcClient(mock);
    BOOST_CHECK_THROW(kms.getDataKey("c", false), KeyCenterConnectionError);
}

BOOST_AUTO_TEST_CASE(clearCacheForcesNextCallToReFetch)
{
    auto mock = std::make_shared<MockKmsClient>();
    bytes realKey{0x99};
    Json::Value rsp;
    rsp["error"] = 0;
    rsp["dataKey"] = toHex(realKey);
    rsp["info"] = "";
    mock->response = rsp;

    BcosKms kms;
    kms.setKcClient(mock);
    kms.getDataKey("c2", false);
    kms.clearCache();
    kms.getDataKey("c2", false);  // cache was cleared → must reconnect
    BOOST_CHECK_EQUAL(mock->connectCalls, 2);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
