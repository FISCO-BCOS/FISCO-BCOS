/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(ConsensusStatusTimeoutTest, TestPromptFixture)

// getConsensusStatus is what the ops tool reads; pin the keys it depends on
BOOST_AUTO_TEST_CASE(statusCarriesConsensusTimeoutAndConnectedNodeList)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    auto fixture = createPBFTFixture(cryptoSuite);
    fixture->appendConsensusNode(fixture->nodeID());
    fixture->init();

    std::string statusText;
    fixture->pbft()->asyncGetConsensusStatus([&statusText](Error::Ptr _error, std::string _status) {
        BOOST_CHECK(!_error);
        statusText = std::move(_status);
    });
    BOOST_REQUIRE(!statusText.empty());

    Json::Value status;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    BOOST_REQUIRE(
        reader->parse(statusText.data(), statusText.data() + statusText.size(), &status, &errors));
    BOOST_REQUIRE(status.isMember("consensusTimeout"));
    BOOST_CHECK_EQUAL(
        status["consensusTimeout"].asUInt64(), fixture->pbftConfig()->consensusTimeout());
    BOOST_CHECK(status.isMember("connectedNodeList"));
    BOOST_CHECK(status.isMember("timeout"));
    BOOST_CHECK(status.isMember("minRequiredQuorum"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
