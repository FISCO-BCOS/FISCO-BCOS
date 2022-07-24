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
 * @brief NodeService.h
 * @file NodeService.h
 * @author: yujiechen
 * @date 2021-10-11
 */
#pragma once
#include "bcos-tars-protocol/Common.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/consensus/ConsensusInterface.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/multigroup/ChainNodeInfo.h>
#include <bcos-framework/multigroup/GroupInfo.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tars-protocol/client/LedgerServiceClient.h>
#include <servant/Application.h>
#include <utility>
namespace bcos
{
namespace rpc
{
class NodeService
{
public:
    using Ptr = std::shared_ptr<NodeService>;
    NodeService(bcos::ledger::LedgerInterface::Ptr _ledger,
        std::shared_ptr<bcos::scheduler::SchedulerInterface> _scheduler,
        bcos::txpool::TxPoolInterface::Ptr _txpool,
        bcos::consensus::ConsensusInterface::Ptr _consensus,
        bcos::sync::BlockSyncInterface::Ptr _sync, bcos::protocol::BlockFactory::Ptr _blockFactory)
      : m_ledger(_ledger),
        m_scheduler(_scheduler),
        m_txpool(_txpool),
        m_consensus(_consensus),
        m_sync(_sync),
        m_blockFactory(_blockFactory)
    {}
    virtual ~NodeService() {}

    bcos::ledger::LedgerInterface::Ptr ledger() { return m_ledger; }
    std::shared_ptr<bcos::scheduler::SchedulerInterface> scheduler() { return m_scheduler; }
    bcos::txpool::TxPoolInterface::Ptr txpool() { return m_txpool; }
    bcos::consensus::ConsensusInterface::Ptr consensus() { return m_consensus; }
    bcos::sync::BlockSyncInterface::Ptr sync() { return m_sync; }
    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }

    void setLedgerPrx(bcostars::LedgerServicePrx const& _ledgerPrx) { m_ledgerPrx = _ledgerPrx; }

    bool unreachable()
    {
        return !bcostars::checkConnection(
            "NodeService", "unreachable", m_ledgerPrx, nullptr, false);
    }

private:
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    std::shared_ptr<bcos::scheduler::SchedulerInterface> m_scheduler;
    bcos::txpool::TxPoolInterface::Ptr m_txpool;
    bcos::consensus::ConsensusInterface::Ptr m_consensus;
    bcos::sync::BlockSyncInterface::Ptr m_sync;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;

    bcostars::LedgerServicePrx m_ledgerPrx;
};

class NodeServiceFactory
{
public:
    using Ptr = std::shared_ptr<NodeServiceFactory>;
    NodeServiceFactory() = default;
    virtual ~NodeServiceFactory() {}
    NodeService::Ptr buildNodeService(std::string const& _chainID, std::string const& _groupID,
        bcos::group::ChainNodeInfo::Ptr _nodeInfo);

    template <typename T, typename S, typename... Args>
    inline std::pair<std::shared_ptr<T>, S> createServicePrx(bcos::protocol::ServiceType _type,
        bcos::group::ChainNodeInfo::Ptr _nodeInfo, const Args&... _args)
    {
        auto serviceName = _nodeInfo->serviceName(_type);
        if (serviceName.size() == 0)
        {
            return std::make_pair(nullptr, nullptr);
        }

        auto prx = bcostars::createServantPrx<S>(serviceName);
        auto client = std::make_shared<T>(prx, _args...);

        return std::make_pair(client, prx);
    }
};
}  // namespace rpc
}  // namespace bcos