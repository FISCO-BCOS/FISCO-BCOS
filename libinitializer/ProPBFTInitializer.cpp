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
 * @file ProPBFTInitializer.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "ProPBFTInitializer.h"
#include <bcos-pbft/pbft/PBFTImpl.h>
#include <bcos-sealer/Sealer.h>
#include <bcos-sync/BlockSync.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tars-protocol/client/RpcServiceClient.h>
#include <bcos-tars-protocol/protocol/GroupInfoCodecImpl.h>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::initializer;

ProPBFTInitializer::ProPBFTInitializer(bcos::initializer::NodeArchitectureType _nodeArchType,
    bcos::tool::NodeConfig::Ptr _nodeConfig, ProtocolInitializer::Ptr _protocolInitializer,
    bcos::txpool::TxPoolInterface::Ptr _txpool, std::shared_ptr<bcos::ledger::Ledger> _ledger,
    bcos::scheduler::SchedulerInterface::Ptr _scheduler,
    bcos::storage::StorageInterface::Ptr _storage,
    std::shared_ptr<bcos::front::FrontServiceInterface> _frontService)
  : PBFTInitializer(_nodeArchType, _nodeConfig, _protocolInitializer, _txpool, _ledger, _scheduler,
        _storage, _frontService)
{
    m_timer = std::make_shared<Timer>(m_timerSchedulerInterval, "node info report");
    // init rpc client
    auto rpcServiceName = m_nodeConfig->rpcServiceName();
    auto rpcServicePrx =
        Application::getCommunicator()->stringToProxy<bcostars::RpcServicePrx>(rpcServiceName);
    m_rpc = std::make_shared<bcostars::RpcServiceClient>(rpcServicePrx, rpcServiceName);

    // init gateway client
    auto gatewayServiceName = m_nodeConfig->gatewayServiceName();
    auto gatewayServicePrx =
        Application::getCommunicator()->stringToProxy<bcostars::GatewayServicePrx>(
            gatewayServiceName);
    m_gateway =
        std::make_shared<bcostars::GatewayServiceClient>(gatewayServicePrx, gatewayServiceName);
}

void ProPBFTInitializer::scheduledTask()
{
    // not enable failover, report nodeInfo to rpc/gw periodly
    reportNodeInfo();
    m_timer->restart();
    return;
}

void ProPBFTInitializer::reportNodeInfo()
{
    // notify groupInfo to rpc
    m_rpc->asyncNotifyGroupInfo(m_groupInfo, [](bcos::Error::Ptr&& _error) {
        if (_error)
        {
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("reportNodeInfo to rpc error") << LOG_KV("code", _error->errorCode())
                << LOG_KV("msg", _error->errorMessage());
        }
    });

    // notify groupInfo to gateway
    m_gateway->asyncNotifyGroupInfo(m_groupInfo, [](bcos::Error::Ptr&& _error) {
        if (_error)
        {
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("reportNodeInfo to gateway error")
                << LOG_KV("code", _error->errorCode()) << LOG_KV("msg", _error->errorMessage());
        }
    });
}

void ProPBFTInitializer::start()
{
    PBFTInitializer::start();
    if (m_timer && !m_nodeConfig->enableFailOver())
    {
        m_timer->start();
    }
}

void ProPBFTInitializer::stop()
{
    if (m_timer)
    {
        m_timer->stop();
    }
    PBFTInitializer::stop();
}

void ProPBFTInitializer::onGroupInfoChanged()
{
    if (!m_leaderElection)
    {
        reportNodeInfo();
        return;
    }
    PBFTInitializer::onGroupInfoChanged();
}


void ProPBFTInitializer::init()
{
    m_timer->registerTimeoutHandler(boost::bind(&ProPBFTInitializer::scheduledTask, this));
    m_blockSync->config()->registerOnNodeTypeChanged([this](bcos::protocol::NodeType _type) {
        INITIALIZER_LOG(INFO) << LOG_DESC("OnNodeTypeChange") << LOG_KV("type", _type)
                              << LOG_KV("nodeName", m_nodeConfig->nodeName());
        auto nodeInfo = m_groupInfo->nodeInfo(m_nodeConfig->nodeName());
        if (!nodeInfo)
        {
            INITIALIZER_LOG(WARNING) << LOG_DESC("failed to find the given node information")
                                     << LOG_KV("node", m_nodeConfig->nodeName());
            return;
        }
        nodeInfo->setNodeType(_type);
        onGroupInfoChanged();
    });
    PBFTInitializer::init();
    reportNodeInfo();
}