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
 * @brief : common functions and types of Blocksync modules
 * @author: yujiechen
 * @date: 2018-09-21
 */

/**
 * @brief : common functions and types of Blocksync modules
 * @author: jimmyshi
 * @date: 2018-10-15
 */
#pragma once
#include <libdevcore/Exceptions.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/Block.h>
#include <libp2p/Common.h>
#include <set>

namespace dev
{
namespace sync
{
static unsigned const c_maxSendTransactions = 10;
static size_t const c_maxDownloadingBlockQueueSize = 1000;
static size_t const c_maxDownloadingBlockQueueBufferSize = 1000;

using NodeList = std::set<dev::p2p::NodeID>;
using NodeID = dev::p2p::NodeID;
using NodeIDs = std::vector<dev::p2p::NodeID>;
using BlockPtr = std::shared_ptr<dev::eth::Block>;
using BlockPtrVec = std::vector<BlockPtr>;

enum SyncPacketType : byte
{
    StatusPacket = 0x00,
    TransactionsPacket = 0x01,
    BlockPacket = 0x02,
    GetBlockPacket = 0x03,
    PacketCount
};

enum class SyncState
{
    Idle,     ///< Initial chain sync complete. Waiting for new packets
    Waiting,  ///< Block downloading paused. Waiting for block queue to process blocks and free
              ///< space
    Blocks,   ///< Downloading blocks
    Size      /// Must be kept last
};

struct NodeInfo
{
    NodeID nodeId;
    int64_t number;
    h256 genesisHash;
    h256 latestHash;
};

}  // namespace sync
}  // namespace dev
