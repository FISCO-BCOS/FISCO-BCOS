/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief : test_VRFBasedPBFT.cpp
 * @author: yujiechen
 * @date: 2020-07-07
 */
#include "test_VRFBasedrPBFT.h"
#include <libconsensus/rotating_pbft/vrf_rpbft/Common.h>
#include <libprecompiled/WorkingSealerManagerPrecompiled.h>
#include <test/unittests/libconsensus/PBFTEngine.h>

using namespace bcos::test;
using namespace bcos::precompiled;

BOOST_FIXTURE_TEST_SUITE(VRFBasedrPBFTTest, TestOutputHelperFixture)

void fakeLeader(VRFBasedrPBFTFixture::Ptr _fixture)
{
    auto pbftEngine = _fixture->vrfBasedrPBFT();
    pbftEngine->setLocatedInConsensusNodes(true);
    auto leader = pbftEngine->getLeader();
    BOOST_CHECK(leader.first == true);
    bcos::h512 leaderNodeID;
    pbftEngine->wrapperGetNodeIDByIndex(leaderNodeID, leader.second);
    BOOST_CHECK(leaderNodeID != bcos::h512());
    BOOST_CHECK(_fixture->nodeID2KeyPair.count(leaderNodeID));
    auto keyPair = (_fixture->nodeID2KeyPair)[leaderNodeID];
    pbftEngine->setKeyPair(keyPair);
}

BOOST_AUTO_TEST_CASE(testVRFBasedrPBFT)
{
    // create fixture with 10 sealers
    auto fixture = std::make_shared<VRFBasedrPBFTFixture>(10);
    auto fakedSealer = fixture->vrfBasedrPBFTSealer();
    auto engine = fixture->vrfBasedrPBFT();
    auto fakeConsensus = fixture->fakerPBFT();
    BOOST_CHECK(engine->shouldRotateSealers() == false);
    // check epoch_sealer_num and epoch_block_num
    BOOST_CHECK(engine->epochSealerNum() == 10);
    BOOST_CHECK(engine->epochBlockNum() == 10);
    // 10 working sealer
    BOOST_CHECK(engine->consensusList().size() == 10);

    auto prepareReq = std::make_shared<PrepareReq>();
    TestIsValidPrepare(*fakeConsensus, prepareReq, true);

    // fake the node to leader
    fakeLeader(fixture);

    // engine notify the sealer to rotate workingSealers
    engine->setShouldRotateSealers(true);
    fakedSealer->hookAfterHandleBlock();

    // check the first transaction is the WorkingSealerManagerPrecompiled
    auto rotatingTx = (*(fakedSealer->mutableSealing().block->transactions()))[0];
    // check from
    BOOST_CHECK(rotatingTx->from() == toAddress(engine->keyPair().pub()));
    // check to
    BOOST_CHECK(rotatingTx->to() == WORKING_SEALER_MGR_ADDRESS);
    // check transaction signature
    BOOST_CHECK(rotatingTx->sender() == toAddress(engine->keyPair().pub()));
    // check transaction type
    BOOST_CHECK(rotatingTx->type() == bcos::protocol::Transaction::Type::MessageCall);
    // check input
    auto const& txData = rotatingTx->data();
    ContractABICodec abi;
    std::string vrfPublicKey;
    std::string blockHashStr;
    std::string vrfProof;
    string interface = WSM_METHOD_ROTATE_STR;

    auto selector = getFuncSelectorByFunctionName(interface);
    auto functionSelector = getParamFunc(ref(rotatingTx->data()));
    BOOST_CHECK(functionSelector == selector);

    auto paramData = getParamData(ref(txData));
    BOOST_CHECK_NO_THROW(abi.abiOut(paramData, vrfPublicKey, blockHashStr, vrfProof));
    // check params
    auto blockNumber = engine->blockChain()->number();
    auto blockHash = engine->blockChain()->numberHash(blockNumber);
    // check vrf input
    BOOST_CHECK(h256(blockHashStr) == blockHash);
    // check vrf publicKey
    BOOST_CHECK(fakedSealer->vrfPublicKey() == vrfPublicKey);
    // check vrfProof
    BOOST_CHECK(
        curve25519_vrf_verify(vrfPublicKey.c_str(), blockHashStr.c_str(), vrfProof.c_str()) == 0);

    // test checkTransactionsValid
    // valid block with rotatingTx
    BOOST_CHECK_NO_THROW(
        engine->checkTransactionsValid(fakedSealer->mutableSealing().block, prepareReq));

    auto orgSender = rotatingTx->sender();
    rotatingTx->forceSender(toAddress(KeyPair::create().pub()));
    BOOST_CHECK_THROW(
        engine->checkTransactionsValid(fakedSealer->mutableSealing().block, prepareReq),
        InvalidNodeRotationTx);

    // pbftBackupBlock: update pbftBackup
    engine->reqCache()->addRawPrepare(prepareReq);
    engine->reqCache()->updateCommittedPrepare();
    // modify sender of block
    LOG(DEBUG) << LOG_DESC("updateCommittedPrepare and checkTransactionsValid");
    BOOST_CHECK_NO_THROW(
        engine->checkTransactionsValid(fakedSealer->mutableSealing().block, prepareReq));
    rotatingTx->forceSender(orgSender);

    // block without invalid rotatingTx
    // invalid receiveAddress
    auto orgTo = rotatingTx->to();
    rotatingTx->setReceiveAddress(Address(0x001));
    BOOST_CHECK_THROW(
        engine->checkTransactionsValid(fakedSealer->mutableSealing().block, prepareReq),
        InvalidNodeRotationTx);
    rotatingTx->setReceiveAddress(orgTo);

    // disable pbftBackup cache
    engine->reqCache()->mutableCommittedPrepareCache()->height = 3;
    // invalid sender
    rotatingTx->forceSender(toAddress(KeyPair::create().pub()));
    BOOST_CHECK_THROW(
        engine->checkTransactionsValid(fakedSealer->mutableSealing().block, prepareReq),
        InvalidNodeRotationTx);

    // receive pbftBackup from other leader(the sealer of the block is the sender of the
    // transaction)
    auto idx = (prepareReq->idx + 1) % (engine->consensusList().size() + 1);
    bcos::h512 nodeId;
    engine->wrapperGetNodeIDByIndex(nodeId, idx);
    rotatingTx->forceSender(toAddress(nodeId));
    fakedSealer->mutableSealing().block->header().setSealer(idx);
    BOOST_CHECK_NO_THROW(
        engine->checkTransactionsValid(fakedSealer->mutableSealing().block, prepareReq));

#if 0
        auto pbftEngine = _fixture->vrfBasedrPBFT();
    pbftEngine->setLocatedInConsensusNodes(true);
    auto leader = pbftEngine->getLeader();
    BOOST_CHECK(leader.first == true);
    bcos::h512 leaderNodeID;
    pbftEngine->wrapperGetNodeIDByIndex(leaderNodeID, leader.second);
    BOOST_CHECK(leaderNodeID != bcos::h512());
    BOOST_CHECK(_fixture->nodeID2KeyPair.count(leaderNodeID));
    auto keyPair = (_fixture->nodeID2KeyPair)[leaderNodeID];
    pbftEngine->setKeyPair(keyPair);
#endif

    // test resetConfig
    // set epoch_block_num to 5(the current block number is 4)
    auto storageFixture = fixture->m_workingSealerManager;
    storageFixture->setSystemConfigByKey(SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, "4");
    BOOST_CHECK(
        engine->blockChain()->getSystemConfigByKey(SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM) == "4");
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == true);
    BOOST_CHECK(engine->minValidNodes() == 7);

    storageFixture->setSystemConfigByKey(SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, "3");
    BOOST_CHECK(
        engine->blockChain()->getSystemConfigByKey(SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM) == "3");
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == false);

    // current workingSealer size is 5, should rotate for the size of current working sealer is
    // smaller than sealer
    storageFixture->updateNodeListType(fixture->sealerList, 5, NODE_TYPE_SEALER);
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == true);
    storageFixture->setSystemConfigByKey(SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, "5");
    BOOST_CHECK(
        engine->blockChain()->getSystemConfigByKey(SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM) == "5");
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == true);
    // should not rotate for the size of current working sealer is equal to the size of the sealers
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == false);

    // reset "notify_rotate" to 1
    storageFixture->setSystemConfigByKey("notify_rotate", "1");
    BOOST_CHECK(engine->blockChain()->getSystemConfigByKey("notify_rotate") == "1");
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == true);

    // reset "notify_rotate" to 0
    storageFixture->setSystemConfigByKey("notify_rotate", "0");
    BOOST_CHECK(engine->blockChain()->getSystemConfigByKey("notify_rotate") == "0");
    engine->resetConfig();
    BOOST_CHECK(engine->shouldRotateSealers() == false);
}

BOOST_AUTO_TEST_CASE(testBasicFunctionForVRFBasedrPBFTEngine) {}

BOOST_AUTO_TEST_SUITE_END()
