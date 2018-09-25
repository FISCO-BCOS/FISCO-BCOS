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
#include <libdevcore/Worker.h>
#include <libethcore/Common.h>
namespace dev
{
class ConsensusStatus;
namespace consensus
{
class ConsensusInterface : public Worker
{
public:
    ConsensusInterface() = default;
    virtual ~ConsensusInterface(){};

    /// start the consensus module
    virtual void start() = 0;
    /// stop the consensus module
    virtual void stop() = 0;

    /// get miner list
    virtual h512s getMinerList() const = 0;
    /// set the miner list
    virtual void setMinerList(h512s const& _minerList) const = 0;

    /// get status of consensus
    virtual ConsensusStatus consensusStatus() const = 0;

    /// protocol id used when register handler to p2p module
    virtual int32_t const& getProtocolId() const = 0;
    virtual void setProtocolId(uint32_t const _protocolId) = 0;
};
}  // namespace consensus
}  // namespace dev
