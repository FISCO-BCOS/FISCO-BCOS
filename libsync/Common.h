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
#include <libnetwork/Common.h>
#include <libp2p/Common.h>

#include <libethcore/Block.h>
#include <libutilities/Exceptions.h>
#include <libutilities/FixedHash.h>


#include <set>

namespace bcos
{
namespace sync
{
DERIVE_BCOS_EXCEPTION(SyncVerifyHandlerNotSet);
DERIVE_BCOS_EXCEPTION(InValidSyncPacket);
// Every downloading request timeout request:
// c_maxRequestBlocks(each peer) * c_maxRequestShards(peer num) = blocks
static int64_t const c_maxRequestBlocks = 32;
static size_t const c_maxRequestShards = 4;
static uint64_t const c_eachBlockDownloadingRequestTimeout =
    200;  // ms: assume that we have 200ms timeout for each block

static size_t const c_maxDownloadingBlockQueueSize =
    c_maxRequestShards * c_maxRequestBlocks * 2;  // maybe less than 128 is ok
static size_t const c_maxDownloadingBlockQueueBufferSize = c_maxDownloadingBlockQueueSize;

static size_t const c_maxReceivedDownloadRequestPerPeer = 1000;
static uint64_t const c_respondDownloadRequestTimeout = (200 * c_maxRequestBlocks);  // ms

static unsigned const c_syncPacketIDBase = 1;

static uint64_t const c_maintainBlocksTimeout = 5000;  // ms

using NodeList = std::set<bcos::p2p::NodeID>;
using NodeID = bcos::p2p::NodeID;
using NodeIDs = std::vector<bcos::p2p::NodeID>;
using BlockPtr = std::shared_ptr<bcos::eth::Block>;
using BlockPtrVec = std::vector<BlockPtr>;

#define PUBLIC_LOG LOG_BADGE("SYNC") << "[id:" << m_nodeId.abridged() << "]"

#define SYNC_LOG(_OBV) LOG(_OBV) << PUBLIC_LOG
#define SYNC_ENGINE_LOG(_OBV) LOG(_OBV) << "[g:" << std::to_string(m_groupId) << "] " << PUBLIC_LOG

enum SyncPacketType : byte
{
    StatusPacket = 0x00,
    TransactionsPacket = 0x01,
    BlocksPacket = 0x02,
    ReqBlocskPacket = 0x03,
    TxsStatusPacket = 0x04,
    TxsRequestPacekt = 0x05,
    PacketCount
};

enum class SyncState
{
    Idle,         ///< Initial chain sync complete. Waiting for new packets
    Downloading,  ///< Downloading blocks
    Size          /// Must be kept last
};
}  // namespace sync
}  // namespace bcos
