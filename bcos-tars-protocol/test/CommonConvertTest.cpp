/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-tars-protocol/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;

namespace bcos::test
{
namespace
{
crypto::KeyInterface::Ptr makeNodeId(crypto::KeyFactory::Ptr const& factory, uint8_t seed)
{
    bcos::bytes raw(32, seed);
    return factory->createKey(raw);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TarsCommonConvertTest)

BOOST_AUTO_TEST_CASE(ledgerConfigRoundTrip)
{
    auto keyFactory = std::make_shared<crypto::KeyFactoryImpl>();
    auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();

    consensus::ConsensusNodeList sealers;
    sealers.push_back(consensus::ConsensusNode{.nodeID = makeNodeId(keyFactory, 0x11),
        .type = consensus::Type::consensus_sealer,
        .voteWeight = 2,
        .termWeight = 3,
        .enableNumber = 0});
    ledgerConfig->setConsensusNodeList(sealers);

    consensus::ConsensusNodeList observers;
    observers.push_back(consensus::ConsensusNode{.nodeID = makeNodeId(keyFactory, 0x22),
        .type = consensus::Type::consensus_observer,
        .voteWeight = 1,
        .termWeight = 0,
        .enableNumber = 0});
    ledgerConfig->setObserverNodeList(observers);

    ledgerConfig->setBlockNumber(42);
    ledgerConfig->setBlockTxCountLimit(1000);
    ledgerConfig->setLeaderSwitchPeriod(7);
    ledgerConfig->setSealerId(3);
    ledgerConfig->setGasLimit(std::make_tuple(3000000000ULL, 42));
    ledgerConfig->setCompatibilityVersion(5);
    ledgerConfig->setHash(crypto::HashType(123));

    auto tars = bcostars::toTarsLedgerConfig(ledgerConfig);
    BOOST_CHECK_EQUAL(tars.blockNumber, 42);
    BOOST_CHECK_EQUAL(tars.blockTxCountLimit, 1000);
    BOOST_CHECK_EQUAL(tars.consensusNodeList.size(), 1U);
    BOOST_CHECK_EQUAL(tars.observerNodeList.size(), 1U);

    auto back = bcostars::toLedgerConfig(tars, keyFactory);
    BOOST_CHECK_EQUAL(back->blockNumber(), 42);
    BOOST_CHECK_EQUAL(back->blockTxCountLimit(), 1000);
    BOOST_CHECK_EQUAL(back->leaderSwitchPeriod(), 7);
    BOOST_CHECK_EQUAL(back->sealerId(), 3);
    BOOST_CHECK_EQUAL(back->compatibilityVersion(), 5U);
    BOOST_CHECK_EQUAL(back->consensusNodeList().size(), 1U);
    BOOST_CHECK_EQUAL(back->observerNodeList().size(), 1U);
    BOOST_CHECK_EQUAL(back->consensusNodeList()[0].voteWeight, 2U);
}

BOOST_AUTO_TEST_CASE(toTarsLedgerConfigNullReturnsEmpty)
{
    auto tars = bcostars::toTarsLedgerConfig(nullptr);
    BOOST_CHECK_EQUAL(tars.blockNumber, 0);
    BOOST_CHECK(tars.consensusNodeList.empty());
}

BOOST_AUTO_TEST_CASE(twoPCParamsRoundTrip)
{
    bcos::protocol::TwoPCParams params;
    params.number = 99;
    params.primaryKey = "pk";
    params.timestamp = 123456;

    auto tars = bcostars::toTarsTwoPCParams(params);
    BOOST_CHECK_EQUAL(tars.blockNumber, 99);
    BOOST_CHECK_EQUAL(tars.primaryKey, "pk");

    auto back = bcostars::toBcosTwoPCParams(tars);
    BOOST_CHECK_EQUAL(back.number, 99);
    BOOST_CHECK_EQUAL(back.primaryKey, "pk");
    BOOST_CHECK_EQUAL(back.timestamp, 123456U);
}

BOOST_AUTO_TEST_CASE(logEntryRoundTripAndTake)
{
    bcos::bytes address{0x01, 0x02, 0x03};
    std::vector<bcos::h256> topics{bcos::h256(1), bcos::h256(2)};
    bcos::bytes data{0xaa, 0xbb};
    bcos::protocol::LogEntry entry(address, topics, data);

    auto tars = bcostars::toTarsLogEntry(entry);
    BOOST_CHECK_EQUAL(tars.topic.size(), 2U);
    BOOST_CHECK_EQUAL(tars.address.size(), 3U);

    auto back = bcostars::toBcosLogEntry(tars);
    BOOST_CHECK_EQUAL(back.topics().size(), 2U);
    BOOST_CHECK(back.data().toBytes() == data);

    // takeToBcosLogEntry moves out of the tars struct
    auto tars2 = bcostars::toTarsLogEntry(entry);
    auto taken = bcostars::takeToBcosLogEntry(std::move(tars2));
    BOOST_CHECK_EQUAL(taken.topics().size(), 2U);
    BOOST_CHECK_EQUAL(taken.address().size(), 3U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
