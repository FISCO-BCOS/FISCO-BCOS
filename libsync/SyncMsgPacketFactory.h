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
 * @brief : implementation of VRF based rPBFTEngine
 * @file: SyncMsgPacketFactory.h
 * @author: yujiechen
 * @date: 2020-06-12
 */

#pragma once
#include "SyncMsgPacket.h"

namespace bcos
{
namespace sync
{
class SyncMsgPacketFactory
{
public:
    using Ptr = std::shared_ptr<SyncMsgPacketFactory>;
    SyncMsgPacketFactory() = default;
    virtual ~SyncMsgPacketFactory() {}

    virtual SyncStatusPacket::Ptr createSyncStatusPacket()
    {
        return std::make_shared<SyncStatusPacket>();
    }
    virtual SyncStatusPacket::Ptr createSyncStatusPacket(NodeID const& _nodeId,
        int64_t const& _number, h256 const& _genesisHash, h256 const& _latestHash)
    {
        return std::make_shared<SyncStatusPacket>(_nodeId, _number, _genesisHash, _latestHash);
    }
};

class SyncMsgPacketWithAlignedTimeFactory : public SyncMsgPacketFactory
{
public:
    using Ptr = std::shared_ptr<SyncMsgPacketWithAlignedTimeFactory>;

    SyncMsgPacketWithAlignedTimeFactory() : SyncMsgPacketFactory() {}
    ~SyncMsgPacketWithAlignedTimeFactory() override {}

    SyncStatusPacket::Ptr createSyncStatusPacket() override
    {
        return std::make_shared<SyncStatusPacketWithAlignedTime>();
    }

    SyncStatusPacket::Ptr createSyncStatusPacket(NodeID const& _nodeId, int64_t const& _number,
        h256 const& _genesisHash, h256 const& _latestHash) override
    {
        return std::make_shared<SyncStatusPacketWithAlignedTime>(
            _nodeId, _number, _genesisHash, _latestHash);
    }
};
}  // namespace sync
}  // namespace bcos
