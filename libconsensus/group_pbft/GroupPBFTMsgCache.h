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
 * (c) 2016-2019 fisco-dev contributors.
 */


/*/*
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
 * @brief : cache for GroupPBFTMsg
 * @file: GroupPBFTMsgCache.h
 * @author: yujiechen
 *
 * @date: 2018-09-29
 *
 */
#pragma once
#include "GroupPBFTMsg.h"
#include <libconsensus/pbft/PBFTMsgCache.h>

namespace dev
{
namespace consensus
{
class GroupPBFTMsgCache : public PBFTMsgCache
{
public:
    GroupPBFTMsgCache() {}
    virtual ~GroupPBFTMsgCache() {}
    // cache message information into the queue
    bool insertByPacketType(unsigned const& type, std::string const& key) override
    {
        switch (type)
        {
        case GroupPBFTPacketType::SuperSignReqPacket:
            insertMessage(x_kownSuperSignReq, m_kownSuperSignReq, max_kownSuperSignReq, key);
            return true;
        case GroupPBFTPacketType::SuperCommitReqPacket:
            insertMessage(x_kownSuperCommitReq, m_kownSuperCommitReq, max_kownSuperCommitReq, key);
            return true;
        default:
            return PBFTMsgCache::insertByPacketType(type, key);
        }
    }


    bool exists(unsigned const& type, std::string const& key) override
    {
        switch (type)
        {
        case GroupPBFTPacketType::SuperSignReqPacket:
            return exists(x_kownSuperSignReq, m_kownSuperSignReq, key);
        case GroupPBFTPacketType::SuperCommitReqPacket:
            return exists(x_kownSuperCommitReq, m_kownSuperCommitReq, key);
        default:
            return PBFTMsgCache::exists(type, key);
        }
    }

    void clearAll() override
    {
        PBFTMsgCache::clearAll();
        {
            WriteGuard l(x_kownSuperSignReq);
            m_kownSuperSignReq.clear();
        }
        {
            WriteGuard l(x_kownSuperCommitReq);
            m_kownSuperCommitReq.clear();
        }
    }

private:
    // cache for superSignReq
    mutable SharedMutex x_kownSuperSignReq;
    QueueSet<std::string> m_kownSuperSignReq;

    // cache for superCommitReq
    mutable SharedMutex x_kownSuperCommitReq;
    QueueSet<std::string> m_kownSuperCommitReq;

    // limitation of cache size
    static const unsigned max_kownSuperSignReq = 1024;
    static const unsigned max_kownSuperCommitReq = 1024;
};

}  // namespace consensus
}  // namespace dev