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
 * @brief PB implement for BlockSyncMsgInterface
 * @file BlockSyncMsgImpl.h
 * @author: yujiechen
 * @date 2021-05-24
 */
#pragma once
#include "bcos-sync/interfaces/BlockSyncMsgInterface.h"
#include "bcos-sync/protocol/proto/BlockSync.pb.h"
#include "bcos-sync/utilities/Common.h"
#include <bcos-protocol/Common.h>
#include <limits>
#include <utility>
namespace bcos::sync
{
class BlockSyncMsgImpl : virtual public BlockSyncMsgInterface
{
public:
    using Ptr = std::shared_ptr<BlockSyncMsgImpl>;
    BlockSyncMsgImpl() : m_syncMessage(std::make_shared<BlockSyncMessage>()) {}
    explicit BlockSyncMsgImpl(bytesConstRef _data) : BlockSyncMsgImpl() { decode(_data); }

    ~BlockSyncMsgImpl() override = default;

    bytesPointer encode() const override { return bcos::protocol::encodePBObject(m_syncMessage); }
    // Maximum allowed sync message size (256 MB)
    static constexpr size_t c_maxSyncMsgSize = 256 * 1024 * 1024;

    void decode(bytesConstRef _data) override
    {
        if (_data.size() > c_maxSyncMsgSize)
        {
            BOOST_THROW_EXCEPTION(
                bcos::protocol::PBObjectDecodeException() << bcos::errinfo_comment(
                    "BlockSyncMsg exceeds max allowed size, size: " + std::to_string(_data.size()) +
                    ", limit: " + std::to_string(c_maxSyncMsgSize)));
        }
        bcos::protocol::decodePBObject(m_syncMessage, _data);

        // FIB-18-new: BlockResponsePacket must contain at least one blocksData entry.
        // PR #5063 added caller-side bounds checks at blockData(0) call sites; reject the
        // malformed message at the decode boundary so the dispatch + factory overhead is
        // not wasted on a packet that will never carry usable data. Other packet types
        // (BlockStatusPacket, BlockRequestPacket) legitimately have an empty blocksData
        // and are NOT affected by this check.
        if (m_syncMessage->packettype() == BlockSyncPacketType::BlockResponsePacket &&
            m_syncMessage->blocksdata_size() == 0)
        {
            BOOST_THROW_EXCEPTION(
                bcos::protocol::PBObjectDecodeException()
                << bcos::errinfo_comment("BlockResponsePacket with empty blocksData rejected"));
        }
    }

    int32_t version() const override { return m_syncMessage->version(); }
    bcos::protocol::BlockNumber number() const override { return m_syncMessage->number(); }
    int32_t packetType() const override { return m_syncMessage->packettype(); }

    void setVersion(int32_t _version) override { m_syncMessage->set_version(_version); }
    void setNumber(bcos::protocol::BlockNumber _number) override
    {
        m_syncMessage->set_number(_number);
    }

    bcos::protocol::BlockNumber archivedBlockNumber() const override
    {
        return m_syncMessage->archived_number();
    }
    void setArchivedNumber(bcos::protocol::BlockNumber _number) override
    {
        m_syncMessage->set_archived_number(_number);
    }

    size_t blockInterval() const override
    {
        const auto rawInterval = m_syncMessage->block_interval();
        if (rawInterval <= 0)
        {
            return 0;
        }
        if (std::cmp_greater(rawInterval, std::numeric_limits<size_t>::max()))
        {
            return std::numeric_limits<size_t>::max();
        }
        return static_cast<size_t>(rawInterval);
    }
    void setBlockInterval(size_t interval) override
    {
        m_syncMessage->set_block_interval(static_cast<int64_t>((interval)));
    }

    void setPacketType(int32_t packetType) override { m_syncMessage->set_packettype(packetType); }

    std::shared_ptr<BlockSyncMessage> syncMessage() { return m_syncMessage; }

protected:
    std::shared_ptr<BlockSyncMessage> m_syncMessage;
};
}  // namespace bcos::sync