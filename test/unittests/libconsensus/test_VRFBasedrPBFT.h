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
 * @brief : test_VRFBasedPBFT.h
 * @author: yujiechen
 * @date: 2020-07-07
 */
#pragma once
#include <libblockverifier/BlockVerifier.h>
#include <libconsensus/rotating_pbft/vrf_rpbft/VRFBasedrPBFTEngine.h>
#include <libconsensus/rotating_pbft/vrf_rpbft/VRFBasedrPBFTSealer.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libprecompiled/test_WorkingSealerManagerPrecompiled.h>
#include <test/unittests/libsync/FakeBlockSync.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>

using namespace dev::p2p;
using namespace dev::txpool;
using namespace dev::blockchain;
using namespace dev::sync;
using namespace dev::blockverifier;
using namespace dev::consensus;

namespace dev
{
namespace test
{
class FakeBlockChainForrPBFT : public FakeBlockChain
{
public:
    FakeBlockChainForrPBFT(WorkingSealerManagerFixture::Ptr _storageFixture, uint64_t _blockNum,
        size_t const& _transSize = 5, KeyPair const& _keyPair = KeyPair::create())
      : FakeBlockChain(_blockNum, _transSize, _keyPair), storageFixture(_storageFixture)
    {
        storageFixture->setSystemConfigByKey("tx_count_limit", "1000");
        storageFixture->setSystemConfigByKey("tx_gas_limit", "3000000000");
        storageFixture->setSystemConfigByKey("notify_rotate", "0");
    }

    std::string getSystemConfigByKey(std::string const& _key, int64_t) override
    {
        return storageFixture->getSystemConfigByKey(_key);
    }

    std::pair<std::string, dev::eth::BlockNumber> getSystemConfigInfoByKey(
        std::string const& _key, int64_t const&) override
    {
        return storageFixture->getSystemConfigAndEnableNumByKey(_key);
    }

    dev::h512s sealerList() override
    {
        storageFixture->getSealerList();
        return (storageFixture->m_pendingSealerList) + (storageFixture->m_workingSealerList);
    }

    dev::h512s workingSealerList() override
    {
        storageFixture->getSealerList();
        return (storageFixture->m_workingSealerList);
    }

    dev::h512s observerList() override
    {
        storageFixture->getSealerList();
        return (storageFixture->m_observerList);
    }
    WorkingSealerManagerFixture::Ptr storageFixture;
};


class FakeVRFBasedrPBFTEngine : public VRFBasedrPBFTEngine
{
public:
    using Ptr = std::shared_ptr<FakeVRFBasedrPBFTEngine>;
    FakeVRFBasedrPBFTEngine(P2PInterface::Ptr _service, TxPoolInterface::Ptr _txPool,
        BlockChainInterface::Ptr _blockChain, SyncInterface::Ptr _blockSync,
        BlockVerifierInterface::Ptr _blockVerifier, dev::PROTOCOL_ID const& _protocolId,
        h512s const& _sealerList = h512s(), KeyPair const& _keyPair = KeyPair::create())
      : VRFBasedrPBFTEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList)
    {
        m_blockFactory = std::make_shared<dev::eth::BlockFactory>();
        setEnableTTLOptimize(true);
        createPBFTMsgFactory();
        createPBFTReqCache();
        setEmptyBlockGenTime(1000);
    }

    IDXTYPE minValidNodes() const override { return VRFBasedrPBFTEngine::minValidNodes(); }
    IDXTYPE selectLeader() const override { return VRFBasedrPBFTEngine::selectLeader(); }
    void updateConsensusNodeList() override
    {
        return VRFBasedrPBFTEngine::updateConsensusNodeList();
    }
    void resetConfig() override { return VRFBasedrPBFTEngine::resetConfig(); }
    void checkTransactionsValid(dev::eth::Block::Ptr _block, PrepareReq::Ptr _prepareReq) override
    {
        return VRFBasedrPBFTEngine::checkTransactionsValid(_block, _prepareReq);
    }
    std::shared_ptr<P2PInterface> mutableService() { return m_service; }
    BlockChainInterface::Ptr blockChain() { return m_blockChain; }
};

class FakeVRFBasedrPBFTSealer : public VRFBasedrPBFTSealer
{
public:
    using Ptr = std::shared_ptr<FakeVRFBasedrPBFTSealer>;
    FakeVRFBasedrPBFTSealer(TxPoolInterface::Ptr _txPool, BlockChainInterface::Ptr _blockChain,
        SyncInterface::Ptr _blockSync)
      : VRFBasedrPBFTSealer(_txPool, _blockChain, _blockSync)
    {}

    Sealing& mutableSealing() { return m_sealing; }
    std::string const& vrfPublicKey() { return m_vrfPublicKey; }
};

class VRFBasedrPBFTFixture
{
public:
    using Ptr = std::shared_ptr<VRFBasedrPBFTFixture>;
    explicit VRFBasedrPBFTFixture(size_t _sealersNum) : m_sealersNum(_sealersNum)
    {
        // fake sealer list
        for (size_t i = 0; i < m_sealersNum; i++)
        {
            auto keyPair = KeyPair::create();
            sealerList.push_back(keyPair.pub());
        }
        // create m_workingSealerManager to fake system storage env
        m_workingSealerManager = std::make_shared<WorkingSealerManagerFixture>();
        // update sealerList
        m_workingSealerManager->sealerList = sealerList;
        m_workingSealerManager->initWorkingSealerManagerFixture(
            _sealersNum, _sealersNum, 10, false);
        m_workingSealerManager->updateNodeListType(
            m_workingSealerManager->sealerList, _sealersNum, NODE_TYPE_WORKING_SEALER);
        // create FakeVRFBasedrPBFTEngine
        auto txPoolCreator = std::make_shared<TxPoolFixture>(5, 5);
        auto fakeSync = std::make_shared<FakeBlockSync>();
        auto blockChain = std::make_shared<FakeBlockChainForrPBFT>(m_workingSealerManager, 5, 5);
        m_VRFBasedrPBFT = std::make_shared<FakeVRFBasedrPBFTEngine>(txPoolCreator->m_topicService,
            txPoolCreator->m_txPool, blockChain, fakeSync, std::make_shared<BlockVerifier>(),
            ProtocolID::PBFT, sealerList);
        m_VRFBasedrPBFT->setMaxRequestPrepareWaitTime(10);
        // Note: all the storage env are ready, can call reportBlock
        m_VRFBasedrPBFT->reportBlock(*(blockChain->getBlockByNumber(blockChain->number())));

        // create and init vrfBasedrPBFTSealer
        m_vrfBasedrPBFTSealer = std::make_shared<FakeVRFBasedrPBFTSealer>(
            txPoolCreator->m_txPool, blockChain, fakeSync);
        m_vrfBasedrPBFTSealer->setConsensusEngine(m_VRFBasedrPBFT);
        m_vrfBasedrPBFTSealer->initConsensusEngine();
        m_vrfBasedrPBFTSealer->mutableSealing().setBlockFactory(
            std::make_shared<dev::eth::BlockFactory>());
    }

    FakeVRFBasedrPBFTEngine::Ptr vrfBasedrPBFT() { return m_VRFBasedrPBFT; }
    // get sealersNum
    size_t sealersNum() { return m_sealersNum; }

    FakeVRFBasedrPBFTSealer::Ptr vrfBasedrPBFTSealer() { return m_vrfBasedrPBFTSealer; }

private:
    FakeVRFBasedrPBFTEngine::Ptr m_VRFBasedrPBFT;
    FakeVRFBasedrPBFTSealer::Ptr m_vrfBasedrPBFTSealer;
    WorkingSealerManagerFixture::Ptr m_workingSealerManager;
    dev::h512s sealerList;

    size_t m_sealersNum = 4;
};

}  // namespace test
}  // namespace dev
