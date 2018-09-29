/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @brief : define classes to manager pbft back keys
 * @file: PBFTMsgCache.h
 * @author: yujiechen
 *
 * @date: 2018-09-29
 *
 */

#pragma once
#include <libconsensus/Common.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>
#include <unordered_map>
namespace dev
{
namespace consensus
{
struct PBFTMsgCache
{
public:
    bool insertByPacketType(unsigned const& type, std::string const& key)
    {
        switch (type)
        {
        case PrepareReqPacket:
            insertMessage(x_knownPrepare, m_knownPrepare, c_knownPrepare, key);
            return true;
        case SignReqPacket:
            insertMessage(x_knownSign, m_knownSign, c_knownSign, key);
            return true;
        case CommitReqPacket:
            insertMessage(x_knownCommit, m_knownCommit, c_knownCommit, key);
            return true;
        case ViewChangeReqPacket:
            insertMessage(x_knownViewChange, m_knownViewChange, c_knownViewChange, key);
        default:
            LOG(WARNING) << "Invalid packet type:" << type;
            return false;
        }
    }

    bool exists(unsigned const& type, std::string const& key)
    {
        switch (type)
        {
        case PrepareReqPacket:
            return exists(x_knownPrepare, m_knownPrepare, key);
        case SignReqPacket:
            return exists(x_knownSign, m_knownSign, key);
        case CommitReqPacket:
            return exists(x_knownCommit, m_knownCommit, key);
        case ViewChangeReqPacket:
            return exists(x_knownViewChange, m_knownViewChange, key);
        default:
            LOG(WARNING) << "Invalid packet type:" << type;
            return false;
        }
    }

    bool inline exists(Mutex& lock, QueueSet<std::string>& queue, std::string const& key)
    {
        DEV_GUARDED(lock)
        return queue.exist(key);
    }

    void inline insertMessage(Mutex& lock, QueueSet<std::string>& queue, size_t const& maxCacheSize,
        std::string const& key)
    {
        DEV_GUARDED(lock)
        {
            if (queue.size() > maxCacheSize)
                queue.pop();
            queue.push(key);
        }
    }

    void clearAll()
    {
        DEV_GUARDED(x_knownPrepare)
        m_knownPrepare.clear();
        DEV_GUARDED(x_knownSign)
        m_knownSign.clear();
        DEV_GUARDED(x_knownCommit)
        m_knownCommit.clear();
        DEV_GUARDED(x_knownViewChange)
        m_knownViewChange.clear();
    }

private:
    Mutex x_knownPrepare;
    QueueSet<std::string> m_knownPrepare;
    Mutex x_knownSign;
    QueueSet<std::string> m_knownSign;
    Mutex x_knownCommit;
    QueueSet<std::string> m_knownCommit;
    Mutex x_knownViewChange;
    QueueSet<std::string> m_knownViewChange;

    static const size_t c_knownPrepare = 1024;
    static const size_t c_knownSign = 1024;
    static const size_t c_knownCommit = 1024;
    static const size_t c_knownViewChange = 1024;
};

class PBFTBroadcastCache
{
public:
    bool insertKey(h512 const& nodeId, unsigned const& type, std::string const& key)
    {
        if (!m_broadCastKeyCache.count(nodeId))
            m_broadCastKeyCache[nodeId] = std::make_shared<PBFTMsgCache>();
        return m_broadCastKeyCache[nodeId]->insertByPacketType(type, key);
    }

    bool keyExists(h512 const& nodeId, unsigned const& type, std::string const& key)
    {
        if (!m_broadCastKeyCache.count(nodeId))
            return false;
        return m_broadCastKeyCache[nodeId]->exists(type, key);
    }

    void clearAll()
    {
        for (auto item : m_broadCastKeyCache)
            item.second->clearAll();
    }

private:
    std::unordered_map<h512, std::shared_ptr<PBFTMsgCache>> m_broadCastKeyCache;
};
}  // namespace consensus
}  // namespace dev
