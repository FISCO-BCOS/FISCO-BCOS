/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-S*: TransactionExecutor smoke (HelloWorld deploy+call, MCOPY without Cancun).
 *  @file CompatExecutorSmokeTest.cpp
 */

#include "CompatExecutorSmokeHarness.h"
#include "CompatTestFixture.h"
#include <boost/algorithm/hex.hpp>

#include <boost/test/unit_test.hpp>
namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_FIXTURE_TEST_SUITE(CompatExecutorSmoke, CompatExecutorSmokeFixture)

BOOST_AUTO_TEST_CASE(FC_S_cancun_only_deploy_call)
{
    using compat::CompatFeatureProfile;

    auto env = buildExecutorWithFeatures(CompatFeatureProfile::cancunOnly());
    advanceBlock(env, 1, CompatFeatureProfile::cancunOnly());

    bytes deployInput;
    boost::algorithm::unhex(helloWorldCreationBin, std::back_inserter(deployInput));
    auto deployTx = fakeTransaction(cryptoSuite, keyPair, "", deployInput, "101", 100001, "1", "1");
    txpool->hash2Transaction[deployTx->hash()] = deployTx;
    const std::string sender = boost::algorithm::hex_lower(std::string(deployTx->sender()));

    auto deployParams = std::make_unique<NativeExecutionMessage>();
    deployParams->setContextID(100);
    deployParams->setSeq(0);
    deployParams->setDepth(0);
    deployParams->setOrigin(sender);
    deployParams->setFrom(sender);
    deployParams->setTo(sender);
    deployParams->setStaticCall(false);
    deployParams->setGasAvailable(gas);
    deployParams->setType(NativeExecutionMessage::TXHASH);
    deployParams->setTransactionHash(deployTx->hash());
    deployParams->setCreate(true);

    auto deployResult = dmcExecute(*env.executor, std::move(deployParams));
    BOOST_REQUIRE(deployResult);
    BOOST_CHECK_EQUAL(deployResult->status(), 0);
    BOOST_CHECK_EQUAL(deployResult->evmStatus(), 0);
    BOOST_CHECK(!deployResult->newEVMContractAddress().empty());

    const std::string contractAddress(deployResult->newEVMContractAddress());
    advanceBlock(env, 2, CompatFeatureProfile::cancunOnly());

    constexpr char setFiscoInput[] =
        "4ed3885e0000000000000000000000000000000000000000000000000000000000000020000000000000000000"
        "0000000000000000000000000000000000000000000005666973636f0000000000000000000000000000000000"
        "00000000000000000000";
    bytes callInput;
    boost::algorithm::unhex(
        setFiscoInput, setFiscoInput + sizeof(setFiscoInput) - 1, std::back_inserter(callInput));

    auto callParams = std::make_unique<NativeExecutionMessage>();
    callParams->setContextID(101);
    callParams->setSeq(0);
    callParams->setDepth(0);
    callParams->setFrom(sender);
    callParams->setTo(contractAddress);
    callParams->setOrigin(sender);
    callParams->setStaticCall(false);
    callParams->setGasAvailable(gas);
    callParams->setData(std::move(callInput));
    callParams->setType(NativeExecutionMessage::MESSAGE);

    auto callResult = dmcExecute(*env.executor, std::move(callParams));
    BOOST_REQUIRE(callResult);
    BOOST_CHECK_EQUAL(callResult->status(), 0);
    BOOST_CHECK_EQUAL(callResult->evmStatus(), 0);
}

BOOST_AUTO_TEST_CASE(FC_S_london_rejects_mcopy)
{
    using compat::CompatFeatureProfile;

    auto env = buildExecutorWithFeatures(CompatFeatureProfile::legacyLondon());
    advanceBlock(env, 1, CompatFeatureProfile::legacyLondon());

    bytes input;
    boost::algorithm::unhex(mcopyContractBin, std::back_inserter(input));
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, "101", 100001, "1", "1");
    txpool->hash2Transaction[tx->hash()] = tx;
    const std::string sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(200);
    params->setSeq(0);
    params->setDepth(0);
    params->setOrigin(sender);
    params->setFrom(sender);
    params->setTo(sender);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setData(input);
    params->setType(NativeExecutionMessage::TXHASH);
    params->setTransactionHash(tx->hash());
    params->setCreate(true);

    auto result = dmcExecute(*env.executor, std::move(params));
    BOOST_REQUIRE(result);
    BOOST_CHECK_EQUAL(result->type(), NativeExecutionMessage::REVERT);
    BOOST_CHECK_EQUAL(result->status(), 10);
    BOOST_CHECK_EQUAL(result->evmStatus(), 5);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatExecutorSmoke
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
