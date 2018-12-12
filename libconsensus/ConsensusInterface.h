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
#include <libethcore/Protocol.h>
namespace dev
{
class ConsensusStatus;
namespace consensus
{
class ConsensusInterface
{
public:
    ConsensusInterface() = default;
    virtual ~ConsensusInterface(){};

    /// start the consensus module
    virtual void start() = 0;
    /// stop the consensus module
    virtual void stop() = 0;

    /// get miner list
    virtual h512s minerList() const = 0;
    /// set the miner list
    /// virtual void setMinerList(h512s const& _minerList) = 0;
    virtual void appendMiner(h512 const& _miner) = 0;
    /// get status of consensus
    virtual const std::string consensusStatus() const = 0;

    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& protocolId() const = 0;
    virtual GROUP_ID const& groupId() const = 0;
    /// get node account type
    virtual NodeAccountType accountType() = 0;
    /// set the node account type
    virtual void setNodeAccountType(NodeAccountType const&) = 0;
    virtual IDXTYPE nodeIdx() const = 0;
    /// update the context of PBFT after commit a block into the block-chain
    virtual void reportBlock(dev::eth::Block const& block) = 0;
};
}  // namespace consensus
}  // namespace dev
