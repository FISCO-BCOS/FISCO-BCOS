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
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : external interface of consensus
 * @author: yujiechen
 * @date: 2018-09-21
 */
#pragma once
#include "Common.h"
#include <libprotocol/BlockFactory.h>
#include <libprotocol/CommonProtocolType.h>
namespace bcos
{
namespace sync
{
class NodeTimeMaintenance;
}
namespace consensus
{
class ConsensusInterface
{
public:
    using Ptr = std::shared_ptr<ConsensusInterface>;
    ConsensusInterface() = default;
    virtual ~ConsensusInterface(){};

    /// start the consensus module
    virtual void start() = 0;
    /// stop the consensus module
    virtual void stop() = 0;

    /// get sealer list
    virtual h512s sealerList() const = 0;
    virtual h512s consensusList() const = 0;
    /// set the sealer list
    /// virtual void setSealerList(h512s const& _sealerList) = 0;
    virtual void appendSealer(h512 const& _sealer) = 0;
    /// get status of consensus
    virtual const std::string consensusStatus() = 0;

    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& protocolId() const = 0;
    virtual GROUP_ID groupId() const { return 0; };
    /// get node account type
    virtual NodeAccountType accountType() = 0;
    /// set the node account type
    virtual void setNodeAccountType(NodeAccountType const&) = 0;
    virtual IDXTYPE nodeIdx() const = 0;
    /// update the context of PBFT after commit a block into the block-chain
    virtual void reportBlock(bcos::protocol::Block const& block) = 0;
    virtual uint64_t maxBlockTransactions() { return 1000; }
    virtual VIEWTYPE view() const { return 0; }
    virtual VIEWTYPE toView() const { return 0; }
    virtual void setBlockFactory(bcos::protocol::BlockFactory::Ptr) {}
    virtual void setSupportConsensusTimeAdjust(bool) {}
    virtual void setNodeTimeMaintenance(std::shared_ptr<bcos::sync::NodeTimeMaintenance>) {}
    virtual int64_t getAlignedTime() { return utcTime(); }
};
}  // namespace consensus
}  // namespace bcos
