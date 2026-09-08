/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <bcos-framework/engine/DACaps.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/MinerEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/FeeHistory.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcostars::protocol;

namespace bcos::test
{
namespace
{
BlockHeaderImpl makeLondonParent(bcos::u256 gasLimit, bcos::u256 gasUsed, bcos::u256 baseFee)
{
    BlockHeaderImpl header;
    header.setEthBlockVersion(bcos::protocol::EthBlockVersion::LONDON);
    header.setGasLimit(gasLimit);
    header.setGasUsed(gasUsed);
    header.setBaseFee(baseFee);
    return header;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FeeHistoryTest)

BOOST_AUTO_TEST_CASE(ethNextBaseFeeHoldsAtTarget)
{
    auto parent = makeLondonParent(30'000'000, 15'000'000, 1'000'000'000);
    auto const next = calcEthNextBaseFee(parent);
    BOOST_CHECK_EQUAL(next, bcos::u256(1'000'000'000));
}

BOOST_AUTO_TEST_CASE(opNextBaseFeeFallsBackWithoutHoloceneExtraData)
{
    auto parent = makeLondonParent(30'000'000, 0, 1'000'000'000);
    auto const next = calcOpNextBaseFee(parent);
    BOOST_CHECK_EQUAL(next, bcos::u256(1'000'000'000));
}

BOOST_AUTO_TEST_CASE(pickRewardPercentilesShape)
{
    std::vector<bcos::u256> tips{1, 3, 9};
    std::vector<double> percentiles{25.0, 50.0, 75.0};
    auto rewards = pickRewardPercentiles(tips, percentiles);
    BOOST_REQUIRE_EQUAL(rewards.size(), 3);
    BOOST_CHECK_EQUAL(rewards[0], 1);
    BOOST_CHECK_EQUAL(rewards[1], 3);
    BOOST_CHECK_EQUAL(rewards[2], 3);
}

BOOST_AUTO_TEST_CASE(minerSetMaxDASizeWritesCaps)
{
    auto nodeService = std::make_shared<bcos::rpc::NodeService>(
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    auto daCaps = std::make_shared<bcos::engine::DACaps>();
    nodeService->setDaCaps(daCaps);
    MinerEndpoint endpoint(nodeService);

    Json::Value request(Json::arrayValue);
    request.append("0x64");
    request.append("0x3e8");
    Json::Value response;
    bcos::task::syncWait(endpoint.setMaxDASize(request, response));

    BOOST_CHECK_EQUAL(daCaps->maxTxSize.load(), 100U);
    BOOST_CHECK_EQUAL(daCaps->maxBlockSize.load(), 1000U);
}

BOOST_AUTO_TEST_CASE(minerSetMaxDASizeMissingOnEthereumNode)
{
    auto nodeService = std::make_shared<bcos::rpc::NodeService>(
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    MinerEndpoint endpoint(nodeService);

    Json::Value request(Json::arrayValue);
    request.append("0x1");
    request.append("0x2");
    Json::Value response;
    BOOST_CHECK_THROW(
        bcos::task::syncWait(endpoint.setMaxDASize(request, response)), JsonRpcException);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
