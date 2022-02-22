/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief initializer for the PBFT module
 * @file PBFTInitializer.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "PBFTInitializer.h"
#include "bcos-framework/interfaces/storage/KVStorageHelper.h"
#include <bcos-pbft/pbft/PBFTFactory.h>
#include <bcos-sealer/SealerFactory.h>
#include <bcos-sync/BlockSyncFactory.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tars-protocol/client/RpcServiceClient.h>
#include <bcos-txpool/TxPoolFactory.h>
#include <bcos-utilities/FileUtility.h>
#include <include/BuildInfo.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::consensus;
using namespace bcos::sealer;
using namespace bcos::txpool;
using namespace bcos::sync;
using namespace bcos::ledger;
using namespace bcos::storage;
using namespace bcos::scheduler;
using namespace bcos::initializer;
using namespace bcos::group;

PBFTInitializer::PBFTInitializer(bcos::initializer::NodeArchitectureType _nodeArchType,
    bcos::tool::NodeConfig::Ptr _nodeConfig, ProtocolInitializer::Ptr _protocolInitializer,
    bcos::txpool::TxPoolInterface::Ptr _txpool, std::shared_ptr<bcos::ledger::Ledger> _ledger,
    bcos::scheduler::SchedulerInterface::Ptr _scheduler,
    bcos::storage::StorageInterface::Ptr _storage,
    std::shared_ptr<bcos::front::FrontServiceInterface> _frontService)
  : m_nodeConfig(_nodeConfig),
    m_protocolInitializer(_protocolInitializer),
    m_txpool(_txpool),
    m_ledger(_ledger),
    m_scheduler(_scheduler),
    m_storage(_storage),
    m_frontService(_frontService)
{
    createSealer();
    createPBFT();
    createSync();
    registerHandlers();
    initChainNodeInfo(_nodeArchType, _nodeConfig);
    m_timer = std::make_shared<Timer>(m_timerSchedulerInterval, "node info report");

    m_timer->registerTimeoutHandler(boost::bind(&PBFTInitializer::reportNodeInfo, this));
}

std::string PBFTInitializer::generateGenesisConfig(bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    Json::Value genesisConfig;
    genesisConfig["consensusType"] = _nodeConfig->consensusType();
    genesisConfig["blockTxCountLimit"] = _nodeConfig->ledgerConfig()->blockTxCountLimit();
    genesisConfig["txGasLimit"] = (int64_t)(_nodeConfig->txGasLimit());
    genesisConfig["consensusLeaderPeriod"] = _nodeConfig->ledgerConfig()->leaderSwitchPeriod();
    Json::Value sealerList(Json::arrayValue);
    auto consensusNodeList = _nodeConfig->ledgerConfig()->consensusNodeList();
    for (auto const& node : consensusNodeList)
    {
        Json::Value sealer;
        sealer["nodeID"] = node->nodeID()->hex();
        sealer["weight"] = node->weight();
        sealerList.append(sealer);
    }
    genesisConfig["sealerList"] = sealerList;
    Json::FastWriter fastWriter;
    std::string genesisConfigStr = fastWriter.write(genesisConfig);
    return genesisConfigStr;
}
std::string PBFTInitializer::generateIniConfig(bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    Json::Value iniConfig;
    Json::Value binaryInfo;

    // get the binaryInfo
    binaryInfo["version"] = FISCO_BCOS_PROJECT_VERSION;
    binaryInfo["gitCommitHash"] = FISCO_BCOS_COMMIT_HASH;
    binaryInfo["platform"] = FISCO_BCOS_BUILD_PLATFORM;
    binaryInfo["buildTime"] = FISCO_BCOS_BUILD_TIME;
    iniConfig["binaryInfo"] = binaryInfo;

    iniConfig["chainID"] = _nodeConfig->chainId();
    iniConfig["groupID"] = _nodeConfig->groupId();
    iniConfig["smCryptoType"] = _nodeConfig->smCryptoType();
    iniConfig["isWasm"] = _nodeConfig->isWasm();
    iniConfig["isAuthCheck"] = _nodeConfig->isAuthCheck();
    iniConfig["nodeName"] = _nodeConfig->nodeName();
    iniConfig["nodeID"] = m_protocolInitializer->keyPair()->publicKey()->hex();
    iniConfig["rpcServiceName"] = _nodeConfig->rpcServiceName();
    iniConfig["gatewayServiceName"] = _nodeConfig->gatewayServiceName();
    Json::FastWriter fastWriter;
    std::string iniConfigStr = fastWriter.write(iniConfig);
    return iniConfigStr;
}

void PBFTInitializer::initChainNodeInfo(
    bcos::initializer::NodeArchitectureType _nodeArchType, bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    m_groupInfo = std::make_shared<GroupInfo>(_nodeConfig->chainId(), _nodeConfig->groupId());
    m_groupInfo->setGenesisConfig(generateGenesisConfig(_nodeConfig));
    int32_t nodeType = bcos::group::NodeType::NON_SM_NODE;
    if (_nodeConfig->smCryptoType())
    {
        nodeType = bcos::group::NodeType::SM_NODE;
    }
    bool microServiceMode = true;
    if (_nodeArchType == bcos::initializer::NodeArchitectureType::AIR)
    {
        microServiceMode = false;
    }
    auto chainNodeInfo = std::make_shared<ChainNodeInfo>(_nodeConfig->nodeName(), nodeType);
    chainNodeInfo->setNodeID(m_protocolInitializer->keyPair()->publicKey()->hex());

    chainNodeInfo->setIniConfig(generateIniConfig(_nodeConfig));
    chainNodeInfo->setMicroService(microServiceMode);

    bool useConfigServiceName = false;
    if (_nodeArchType == bcos::initializer::NodeArchitectureType::MAX)
    {
        useConfigServiceName = true;
    }
    auto localNodeServiceName = ServerConfig::Application + "." + ServerConfig::ServerName;
    chainNodeInfo->appendServiceInfo(SCHEDULER,
        useConfigServiceName ? m_nodeConfig->schedulerServiceName() : localNodeServiceName);
    chainNodeInfo->appendServiceInfo(LEDGER,
        useConfigServiceName ? bcostars::getProxyDesc(LEDGER_SERVANT_NAME) : localNodeServiceName);
    chainNodeInfo->appendServiceInfo(
        FRONT, useConfigServiceName ? m_nodeConfig->frontServiceName() : localNodeServiceName);
    chainNodeInfo->appendServiceInfo(CONSENSUS, localNodeServiceName);
    chainNodeInfo->appendServiceInfo(
        TXPOOL, useConfigServiceName ? m_nodeConfig->txpoolServiceName() : localNodeServiceName);
    m_groupInfo->appendNodeInfo(chainNodeInfo);
}

void PBFTInitializer::reportNodeInfo()
{
    asyncNotifyGroupInfo<bcostars::RpcServicePrx, bcostars::RpcServiceClient>(
        m_nodeConfig->rpcServiceName(), m_groupInfo);
    asyncNotifyGroupInfo<bcostars::GatewayServicePrx, bcostars::GatewayServiceClient>(
        m_nodeConfig->gatewayServiceName(), m_groupInfo);
    m_timer->restart();
}

void PBFTInitializer::start()
{
    m_sealer->start();
    m_blockSync->start();
    m_pbft->start();
}

void PBFTInitializer::startReport()
{
    if (m_timer)
    {
        m_timer->start();
    }
}

void PBFTInitializer::stop()
{
    if (m_timer)
    {
        m_timer->stop();
    }
    m_sealer->stop();
    m_blockSync->stop();
    m_pbft->stop();
}

void PBFTInitializer::init()
{
    m_sealer->init(m_pbft);
    m_blockSync->init();
    m_pbft->init();
}

void PBFTInitializer::registerHandlers()
{
    // handler to notify the sealer reset the sealing proposals
    std::weak_ptr<Sealer> weakedSealer = m_sealer;
    m_pbft->registerSealerResetNotifier([weakedSealer](
                                            std::function<void(bcos::Error::Ptr)> _onRecv) {
        try
        {
            auto sealer = weakedSealer.lock();
            if (!sealer)
            {
                return;
            }
            sealer->asyncResetSealing(_onRecv);
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING) << LOG_DESC("call asyncResetSealing to the sealer exception")
                                     << LOG_KV("error", boost::diagnostic_information(e));
        }
    });

    // register handlers for the consensus to interact with the sealer
    m_pbft->registerSealProposalNotifier(
        [weakedSealer](size_t _proposalIndex, size_t _proposalEndIndex, size_t _maxTxsToSeal,
            std::function<void(Error::Ptr)> _onRecvResponse) {
            try
            {
                auto sealer = weakedSealer.lock();
                if (!sealer)
                {
                    return;
                }
                sealer->asyncNotifySealProposal(
                    _proposalIndex, _proposalEndIndex, _maxTxsToSeal, _onRecvResponse);
            }
            catch (std::exception const& e)
            {
                INITIALIZER_LOG(WARNING) << LOG_DESC("call notify proposal sealing exception")
                                         << LOG_KV("error", boost::diagnostic_information(e));
            }
        });

    // the consensus module notify the latest blockNumber to the sealer
    m_pbft->registerStateNotifier([weakedSealer](bcos::protocol::BlockNumber _blockNumber) {
        try
        {
            auto sealer = weakedSealer.lock();
            if (!sealer)
            {
                return;
            }
            sealer->asyncNoteLatestBlockNumber(_blockNumber);
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("call notify the latest block number to the sealer exception")
                << LOG_KV("error", boost::diagnostic_information(e));
        }
    });

    // the consensus moudle notify new block to the sync module
    std::weak_ptr<BlockSyncInterface> weakedSync = m_blockSync;
    m_pbft->registerNewBlockNotifier([weakedSync](bcos::ledger::LedgerConfig::Ptr _ledgerConfig,
                                         std::function<void(Error::Ptr)> _onRecv) {
        try
        {
            auto sync = weakedSync.lock();
            if (!sync)
            {
                return;
            }
            sync->asyncNotifyNewBlock(_ledgerConfig, _onRecv);
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("call notify the latest block to the sync module exception")
                << LOG_KV("error", boost::diagnostic_information(e));
        }
    });

    m_pbft->registerFaultyDiscriminator([weakedSync](bcos::crypto::NodeIDPtr _nodeID) -> bool {
        try
        {
            auto sync = weakedSync.lock();
            if (!sync)
            {
                return false;
            }
            return sync->faultyNode(_nodeID);
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("determine the node is faulty or not through the sync module exception")
                << LOG_KV("node", _nodeID->shortHex())
                << LOG_KV("error", boost::diagnostic_information(e));
        }
        return false;
    });

    m_pbft->registerCommittedProposalNotifier(
        [weakedSync](bcos::protocol::BlockNumber _committedProposal,
            std::function<void(Error::Ptr)> _onRecv) {
            try
            {
                auto sync = weakedSync.lock();
                if (!sync)
                {
                    return;
                }
                sync->asyncNotifyCommittedIndex(_committedProposal, _onRecv);
            }
            catch (std::exception const& e)
            {
                INITIALIZER_LOG(WARNING) << LOG_DESC(
                                                "call notify the latest committed proposal index "
                                                "to the sync module exception")
                                         << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void PBFTInitializer::createSealer()
{
    // create sealer
    auto sealerFactory = std::make_shared<SealerFactory>(
        m_protocolInitializer->blockFactory(), m_txpool, m_nodeConfig->minSealTime());
    m_sealer = sealerFactory->createSealer();
}

void PBFTInitializer::createPBFT()
{
    auto keyPair = m_protocolInitializer->keyPair();
    auto kvStorage = std::make_shared<bcos::storage::KVStorageHelper>(m_storage);
    // create pbft
    auto pbftFactory = std::make_shared<PBFTFactory>(m_protocolInitializer->cryptoSuite(),
        m_protocolInitializer->keyPair(), m_frontService, kvStorage, m_ledger, m_scheduler,
        m_txpool, m_protocolInitializer->blockFactory(), m_protocolInitializer->txResultFactory());

    m_pbft = pbftFactory->createPBFT();
    auto pbftConfig = m_pbft->pbftEngine()->pbftConfig();
    pbftConfig->setCheckPointTimeoutInterval(m_nodeConfig->checkPointTimeoutInterval());
}

void PBFTInitializer::createSync()
{
    // create sync
    auto keyPair = m_protocolInitializer->keyPair();
    auto blockSyncFactory = std::make_shared<BlockSyncFactory>(keyPair->publicKey(),
        m_protocolInitializer->blockFactory(), m_protocolInitializer->txResultFactory(), m_ledger,
        m_txpool, m_frontService, m_scheduler, m_pbft);
    m_blockSync = blockSyncFactory->createBlockSync();
}

std::shared_ptr<bcos::txpool::TxPoolInterface> PBFTInitializer::txpool()
{
    return m_txpool;
}
std::shared_ptr<bcos::sync::BlockSyncInterface> PBFTInitializer::blockSync()
{
    return m_blockSync;
}
std::shared_ptr<bcos::consensus::ConsensusInterface> PBFTInitializer::pbft()
{
    return m_pbft;
}
std::shared_ptr<bcos::sealer::SealerInterface> PBFTInitializer::sealer()
{
    return m_sealer;
}