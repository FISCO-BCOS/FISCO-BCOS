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
#include <bcos-framework/election/FailOverTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/storage/KVStorageHelper.h>

#ifdef WITH_TIKV
#include <bcos-leader-election/src/LeaderElectionFactory.h>
#endif

#include <bcos-pbft/pbft/PBFTFactory.h>
#include <bcos-scheduler/src/SchedulerManager.h>
#include <bcos-sealer/SealerFactory.h>
#include <bcos-sync/BlockSyncFactory.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>
#include <bcos-tars-protocol/protocol/MemberImpl.h>
#include <bcos-txpool/TxPool.h>
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
using namespace bcos::protocol;
using namespace bcos::election;

PBFTInitializer::PBFTInitializer(bcos::protocol::NodeArchitectureType _nodeArchType,
    bcos::tool::NodeConfig::Ptr _nodeConfig, ProtocolInitializer::Ptr _protocolInitializer,
    bcos::txpool::TxPoolInterface::Ptr _txpool, std::shared_ptr<bcos::ledger::Ledger> _ledger,
    bcos::scheduler::SchedulerInterface::Ptr _scheduler,
    bcos::storage::StorageInterface::Ptr _storage,
    std::shared_ptr<bcos::front::FrontServiceInterface> _frontService)
  : m_nodeArchType(_nodeArchType),
    m_nodeConfig(_nodeConfig),
    m_protocolInitializer(_protocolInitializer),
    m_txpool(_txpool),
    m_ledger(_ledger),
    m_scheduler(_scheduler),
    m_storage(_storage),
    m_frontService(_frontService)
{
    m_groupInfoCodec = std::make_shared<bcostars::protocol::GroupInfoCodecImpl>();
    createSealer();
    createPBFT();
    createSync();
    registerHandlers();
    initChainNodeInfo(m_nodeArchType, m_nodeConfig);
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
    iniConfig["isSerialExecute"] = _nodeConfig->isSerialExecute();
    iniConfig["nodeName"] = _nodeConfig->nodeName();
    iniConfig["nodeID"] = m_protocolInitializer->keyPair()->publicKey()->hex();
    iniConfig["rpcServiceName"] = _nodeConfig->rpcServiceName();
    iniConfig["gatewayServiceName"] = _nodeConfig->gatewayServiceName();
    Json::FastWriter fastWriter;
    std::string iniConfigStr = fastWriter.write(iniConfig);
    return iniConfigStr;
}

void PBFTInitializer::initChainNodeInfo(
    bcos::protocol::NodeArchitectureType _nodeArchType, bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    m_groupInfo = std::make_shared<GroupInfo>(_nodeConfig->chainId(), _nodeConfig->groupId());
    m_groupInfo->setGenesisConfig(generateGenesisConfig(_nodeConfig));
    int32_t nodeType = bcos::group::NodeCryptoType::NON_SM_NODE;
    if (_nodeConfig->smCryptoType())
    {
        nodeType = bcos::group::NodeCryptoType::SM_NODE;
    }
    bool microServiceMode = true;
    if (_nodeArchType == bcos::protocol::NodeArchitectureType::AIR)
    {
        microServiceMode = false;
    }

    m_nodeInfo = std::make_shared<ChainNodeInfo>(_nodeConfig->nodeName(), nodeType);
    m_nodeInfo->setNodeID(m_protocolInitializer->keyPair()->publicKey()->hex());

    m_nodeInfo->setIniConfig(generateIniConfig(_nodeConfig));
    m_nodeInfo->setMicroService(microServiceMode);
    m_nodeInfo->setNodeType(m_blockSync->config()->nodeType());
    m_nodeInfo->setNodeCryptoType(
        (_nodeConfig->smCryptoType() ? NodeCryptoType::SM_NODE : NON_SM_NODE));
    if (_nodeArchType == bcos::protocol::NodeArchitectureType::AIR)
    {
        m_nodeInfo->appendServiceInfo(SCHEDULER, SCHEDULER_SERVANT_NAME);
        m_nodeInfo->appendServiceInfo(LEDGER, LEDGER_SERVANT_NAME);
        m_nodeInfo->appendServiceInfo(FRONT, FRONT_SERVANT_NAME);
        m_nodeInfo->appendServiceInfo(TXPOOL, TXPOOL_SERVANT_NAME);
    }
    // Note: must set the serviceInfo for rpc/gateway to pass the groupInfo check when sync latest
    // groupInfo to rpc/gateway service
    m_nodeInfo->appendServiceInfo(GATEWAY, m_nodeConfig->gatewayServiceName());
    m_nodeInfo->appendServiceInfo(RPC, m_nodeConfig->rpcServiceName());
    // set protocolInfo
    auto nodeProtocolInfo = g_BCOSConfig.protocolInfo(ProtocolModuleID::NodeService);
    m_nodeInfo->setNodeProtocol(*nodeProtocolInfo);
    m_nodeInfo->setCompatibilityVersion(m_pbft->compatibilityVersion());
    m_groupInfo->appendNodeInfo(m_nodeInfo);
    INITIALIZER_LOG(INFO) << LOG_DESC("PBFTInitializer::initChainNodeInfo")
                          << LOG_KV("nodeType", m_nodeInfo->nodeType())
                          << LOG_KV("nodeCryptoType", m_nodeInfo->nodeCryptoType())
                          << LOG_KV("nodeName", _nodeConfig->nodeName())
                          << LOG_KV("compatibilityVersion", m_nodeInfo->compatibilityVersion());
}

void PBFTInitializer::start()
{
    if (!m_nodeConfig->enableFailOver())
    {
        m_blockSync->enableAsMaster(true);
        // Note: since enableAsMasterNode will recover pbftState and execute the recovered proposal,
        // should call this after every module and handlers has been inited completed
        m_pbft->enableAsMasterNode(true);
    }
    m_sealer->start();
    m_blockSync->start();
    m_pbft->start();
    if (m_leaderElection)
    {
        m_leaderElection->start();
    }
}

void PBFTInitializer::stop()
{
    if (m_leaderElection)
    {
        m_leaderElection->stop();
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
    if (m_nodeConfig->enableFailOver())
    {
        initConsensusFailOver(m_protocolInitializer->keyPair()->publicKey());
    }
    syncGroupNodeInfo();
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
    m_txpool->registerTxsCleanUpSwitch([this]() -> bool {
        auto config = m_pbft->pbftEngine()->pbftConfig();
        // should clean up expired txs periodically for non-consensus node
        if (!config->isConsensusNode())
        {
            return true;
        }
        // clean up the expired txs for the consensus-timeout node
        return config->timeout();
    });
}

void PBFTInitializer::initNotificationHandlers(bcos::rpc::RPCInterface::Ptr _rpc)
{
    // version notification
    m_pbft->registerVersionInfoNotification([_rpc, this](uint32_t _version) {
        // Note: the nodeInfo and the groupInfo are mutable
        auto nodeInfo = m_groupInfo->nodeInfo(m_nodeConfig->nodeName());
        // Note: notify groupInfo to all rpc nodes in pro/max mode
        nodeInfo->setCompatibilityVersion(_version);
        _rpc->asyncNotifyGroupInfo(m_groupInfo, [_version](bcos::Error::Ptr&& _error) {
            if (!_error)
            {
                INITIALIZER_LOG(WARNING) << LOG_DESC("Election versionInfoNotification success")
                                         << LOG_KV("version", _version);
                return;
            }
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("Election versionInfoNotification error") << LOG_KV("version", _version)
                << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
        });
        onGroupInfoChanged();
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

// sync groupNodeInfo from the gateway
void PBFTInitializer::syncGroupNodeInfo()
{
    // Note: In air mode, the groupNodeInfo must be successful
    auto self = std::weak_ptr<PBFTInitializer>(shared_from_this());
    m_frontService->asyncGetGroupNodeInfo(
        [self](Error::Ptr _error, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo) {
            auto pbftInit = self.lock();
            if (!pbftInit)
            {
                return;
            }
            if (_error != nullptr)
            {
                INITIALIZER_LOG(WARNING)
                    << LOG_DESC("asyncGetGroupNodeInfo failed")
                    << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
                return;
            }
            try
            {
                if (!_groupNodeInfo || _groupNodeInfo->nodeIDList().size() == 0)
                {
                    return;
                }
                NodeIDSet nodeIdSet;
                auto const& nodeIDList = _groupNodeInfo->nodeIDList();
                if (nodeIDList.size() == 0)
                {
                    return;
                }
                for (auto const& nodeIDStr : nodeIDList)
                {
                    auto nodeID =
                        pbftInit->m_protocolInitializer->cryptoSuite()->keyFactory()->createKey(
                            fromHex(nodeIDStr));
                    nodeIdSet.insert(nodeID);
                }
                // the blockSync module set the connected node list
                pbftInit->m_blockSync->config()->setConnectedNodeList(std::move(nodeIdSet));
                // the txpool module set the connected node list
                auto txpool = std::dynamic_pointer_cast<bcos::txpool::TxPool>(pbftInit->m_txpool);
                txpool->transactionSync()->config()->setConnectedNodeList(std::move(nodeIdSet));
                INITIALIZER_LOG(INFO) << LOG_DESC("syncGroupNodeInfo for block sync and txpool")
                                      << LOG_KV("connectedSize", nodeIdSet.size());
            }
            catch (std::exception const& e)
            {
                INITIALIZER_LOG(WARNING) << LOG_DESC("asyncGetGroupNodeInfo exception")
                                         << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void PBFTInitializer::onGroupInfoChanged()
{
    if (!m_leaderElection)
    {
        return;
    }
    // failover enabled, should sync the latest information to the etcd if the node is
    // leader
    INITIALIZER_LOG(INFO) << LOG_DESC("Election onGroupInfoChanged, update the memberConfig");
    std::string modifiedConfig;
    m_groupInfoCodec->serialize(modifiedConfig, m_groupInfo);
    auto memberInfo = m_memberFactory->createMember();
    memberInfo->setMemberID(m_nodeConfig->memberID());
    memberInfo->setMemberConfig(modifiedConfig);
    m_leaderElection->updateSelfConfig(memberInfo);
}

void PBFTInitializer::initConsensusFailOver(KeyInterface::Ptr _nodeID)
{
    m_memberFactory = std::make_shared<bcostars::protocol::MemberFactoryImpl>();

#ifdef WITH_TIKV
    auto leaderElectionFactory = std::make_shared<LeaderElectionFactory>(m_memberFactory);
#endif
    // leader key: /${chainID}/consensus/${nodeID}
    std::string leaderKey =
        "/" + m_nodeConfig->chainId() + bcos::election::CONSENSUS_LEADER_DIR + _nodeID->hex();

    std::string nodeConfig;
    m_groupInfoCodec->serialize(nodeConfig, m_groupInfo);

#ifdef WITH_TIKV
    m_leaderElection = leaderElectionFactory->createLeaderElection(m_nodeConfig->memberID(),
        nodeConfig, m_nodeConfig->failOverClusterUrl(), leaderKey, "consensus_fault_tolerance",
        m_nodeConfig->leaseTTL());


    // register the handler
    m_leaderElection->registerOnCampaignHandler(
        [this](bool _success, bcos::protocol::MemberInterface::Ptr _leader) {
            m_pbft->enableAsMasterNode(_success);
            m_blockSync->enableAsMaster(_success);
            INITIALIZER_LOG(INFO) << LOG_DESC("onCampaignHandler") << LOG_KV("success", _success)
                                  << LOG_KV("leader", _leader ? _leader->memberID() : "None");
            if (!_success)
            {
                return;
            }
            auto schedulerManager =
                std::dynamic_pointer_cast<bcos::scheduler::SchedulerManager>(m_scheduler);
            schedulerManager->asyncSwitchTerm(_leader->seq(), [_leader](Error::Ptr&& error) {
                INITIALIZER_LOG(INFO)
                    << "Notify scheduler switch " << (error ? "failed" : "success") << " with"
                    << LOG_KV("seq", _leader->seq());
            });
        });
    INITIALIZER_LOG(INFO) << LOG_DESC("initConsensusFailOver") << LOG_KV("leaderKey", leaderKey)
                          << LOG_KV("nodeConfig", nodeConfig);
#endif
}