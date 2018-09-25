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
 * @brief : external interfaces of BlockSync module called by RPC and p2p
 * @author: yujiechen
 * @date: 2018-09-21
 */
#pragma once
#include "Common.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
namespace dev
{
namespace sync
{
class SyncStatus;
class SyncInterface : public Worker
{
public:
    SyncInterface() = default;
    virtual ~SyncInterface(){};
    /// start blockSync
    virtual void start() = 0;
    /// stop blockSync
    virtual void stop() = 0;

    /// get status of block sync
    /// @returns Synchonization status
    SyncStatus status() const = 0;
    virtual bool isSyncing() const = 0;
    virtual h256 latestBlockSent() = 0;

    /// for rpc && sdk: broad cast transaction to all nodes
    virtual void broadCastTransactions() = 0;
    /// for p2p: broad cast transaction to specified nodes
    virtual void sendTransactions(NodeList const& _nodes) = 0;

    /// abort sync and reset all status of blockSyncs
    virtual void reset() = 0;
    virtual bool forceSync() = 0;

    /// protocol id used when register handler to p2p module
    virtual int32_t const& getProtocolId() const = 0;
    virtual void setProtocolId(uint32_t const _protocolId) = 0;
}  // namespace sync
}  // namespace sync
