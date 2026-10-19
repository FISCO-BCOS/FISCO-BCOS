/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief the Report line marks a synced block with sealer=-1,txs=-1 even though the sync path
 *        hands PBFTConfig::resetConfig a ledger config filled from the downloaded header
 * @file ReportSyncedBlockTest.cpp
 */
#include "test/unittests/pbft/PBFTFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/testutils/LogCapture.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(ReportSyncedBlockTest, TestPromptFixture)

namespace
{
// a ledger config for the block after the committed one, as the sync path builds it: sealer and
// tx count copied from the downloaded header
ledger::LedgerConfig::Ptr nextBlockConfig(PBFTFixture& _fixture, int64_t _sealer, int64_t _txs)
{
    auto config = std::make_shared<ledger::LedgerConfig>(*_fixture.ledger()->ledgerConfig());
    config->setBlockNumber(_fixture.pbftConfig()->committedProposal()->index() + 1);
    config->setSealerId(_sealer);
    config->setTxsSize(_txs);
    return config;
}
}  // namespace

BOOST_AUTO_TEST_CASE(syncedBlockReportsMinusOne)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto fixture = createPBFTFixture(std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr));
    fixture->appendConsensusNode(fixture->nodeID());
    fixture->init();

    LogCapture synced;
    fixture->pbftConfig()->resetConfig(nextBlockConfig(*fixture, 2, 5), true);
    auto syncedText = synced.text();
    BOOST_CHECK_MESSAGE(
        syncedText.find("Report,sealer=-1,txs=-1,") != std::string::npos, syncedText);
    BOOST_CHECK(syncedText.find("Report,sealer=2,") == std::string::npos);

    LogCapture consensused;
    fixture->pbftConfig()->resetConfig(nextBlockConfig(*fixture, 2, 5), false);
    auto consensusedText = consensused.text();
    BOOST_CHECK_MESSAGE(
        consensusedText.find("Report,sealer=2,txs=5,") != std::string::npos, consensusedText);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
