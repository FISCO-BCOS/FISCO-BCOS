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

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::initializer;

ProPBFTInitializer::ProPBFTInitializer(bcos::protocol::NodeArchitectureType _nodeArchType,
    bcos::tool::NodeConfig::Ptr _nodeConfig, ProtocolInitializer::Ptr _protocolInitializer,
    bcos::txpool::TxPoolInterface::Ptr _txpool, std::shared_ptr<bcos::ledger::Ledger> _ledger,
    bcos::scheduler::SchedulerInterface::Ptr _scheduler,
    bcos::storage::StorageInterface::Ptr _storage,
    std::shared_ptr<bcos::front::FrontServiceInterface> _frontService)
  : PBFTInitializer(_nodeArchType, _nodeConfig, _protocolInitializer, _txpool, _ledger, _scheduler,
        _storage, _frontService)
{
    m_timer = std::make_shared<Timer>(m_timerSchedulerInterval, "node info report");
}

void ProPBFTInitializer::reportNodeInfo()
{
    asyncNotifyGroupInfo<bcostars::RpcServicePrx, bcostars::RpcServiceClient>(
        m_nodeConfig->rpcServiceName(), m_groupInfo);
    asyncNotifyGroupInfo<bcostars::GatewayServicePrx, bcostars::GatewayServiceClient>(
        m_nodeConfig->gatewayServiceName(), m_groupInfo);
    m_timer->restart();
}

void ProPBFTInitializer::start()
{
    PBFTInitializer::start();
    if (m_timer)
    {
        m_timer->start();
    }
    m_sealer->start();
    m_blockSync->start();
    m_pbft->start();
}

void ProPBFTInitializer::stop()
{
    if (m_timer)
    {
        m_timer->stop();
    }
    PBFTInitializer::stop();
}

void ProPBFTInitializer::init()
{
    PBFTInitializer::init();
    m_timer->registerTimeoutHandler(boost::bind(&ProPBFTInitializer::reportNodeInfo, this));
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
        reportNodeInfo();
    });
}