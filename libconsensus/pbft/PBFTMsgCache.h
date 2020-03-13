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
 * @brief : define classes to manager pbft back keys
 * @file: PBFTMsgCache.h
 * @author: yujiechen
 *
 * @date: 2018-09-29
 *
 */

#pragma once
#include <libconsensus/pbft/Common.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <unordered_map>
namespace dev
{
namespace consensus
{
/// cache object of given ndoe
struct PBFTMsgCache
{
public:
    /**
     * @brief: insert given key into the given-type-cache of the given node id
     *
     * @param type : packet type
     * @param key: mainly the signature of specified broadcast packet
     * @return true : insert succeed
     * @return false : insert failed
     */
    inline bool insertByPacketType(unsigned const& type, std::string const& key)
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
            return true;
        default:
            return false;
        }
    }

    /**
     * @brief : given key exists in the given-type-cache of the given node id or not
     *
     * @param type: packet type
     * @param key: mainly the signature of specified broadcast packet
     * @return true: the given key exists
     * @return false: the given key doesn't exist
     */
    inline bool exists(unsigned const& type, std::string const& key)
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
            LOG(DEBUG) << "Invalid packet type:" << type;
            return false;
        }
    }

    inline bool exists(SharedMutex& lock, QueueSet<std::string>& queue, std::string const& key)
    {
        /// lock succ
        ReadGuard l(lock);
        bool exist = queue.exist(key);
        return exist;
    }

    inline void insertMessage(SharedMutex& lock, QueueSet<std::string>& queue,
        size_t const& maxCacheSize, std::string const& key)
    {
        WriteGuard l(lock);
        if (queue.size() > maxCacheSize)
            queue.pop();
        queue.push(key);
    }
    /// clear all the cache
    inline void clearAll()
    {
        {
            WriteGuard l(x_knownPrepare);
            m_knownPrepare.clear();
        }
        {
            WriteGuard l(x_knownSign);
            m_knownSign.clear();
        }
        {
            WriteGuard l(x_knownCommit);
            m_knownCommit.clear();
        }
        {
            WriteGuard l(x_knownViewChange);
            m_knownViewChange.clear();
        }
    }

private:
    /// mutex for m_knownPrepare
    mutable SharedMutex x_knownPrepare;
    /// cache for the prepare packet
    QueueSet<std::string> m_knownPrepare;
    /// mutex for m_knownSign
    mutable SharedMutex x_knownSign;
    /// cache for the sign packet
    QueueSet<std::string> m_knownSign;
    /// mutex for m_knownCommit
    mutable SharedMutex x_knownCommit;
    /// cache for the commit packet
    QueueSet<std::string> m_knownCommit;
    /// mutex for m_knownViewChange
    mutable SharedMutex x_knownViewChange;
    /// cache for the viewchange packet
    QueueSet<std::string> m_knownViewChange;

    /// the limit size for prepare packet cache
    static const unsigned c_knownPrepare = 1024;
    /// the limit size for sign packet cache
    static const unsigned c_knownSign = 1024;
    /// the limit size for commit packet cache
    static const unsigned c_knownCommit = 1024;
    /// the limit size for viewchange packet cache
    static const unsigned c_knownViewChange = 1024;
};

class PBFTBroadcastCache
{
public:
    /**
     * @brief : insert key into the queue according to node id and packet type
     *
     * @param nodeId : node id
     * @param type: packet type
     * @param key: key (mainly the signature of specified broadcast packet)
     * @return true: insert success
     * @return false: insert failed
     */
    inline bool insertKey(h512 const& nodeId, unsigned const& type, std::string const& key)
    {
        if (!m_broadCastKeyCache.count(nodeId))
            m_broadCastKeyCache[nodeId] = std::make_shared<PBFTMsgCache>();
        return m_broadCastKeyCache[nodeId]->insertByPacketType(type, key);
    }

    /**
     * @brief: determine whether the key of given packet type existed in the cache of given node id
     *
     * @param nodeId : node id
     * @param type: packet type
     * @param key: mainly the signature of specified broadcast packe
     * @return true : the key exists in the cache
     * @return false: the key doesn't exist in the cache
     */
    inline bool keyExists(h512 const& nodeId, unsigned const& type, std::string const& key)
    {
        if (!m_broadCastKeyCache.count(nodeId))
            return false;
        return m_broadCastKeyCache[nodeId]->exists(type, key);
    }

    /// clear all caches
    inline void clearAll()
    {
        for (auto& item : m_broadCastKeyCache)
            item.second->clearAll();
    }

private:
    /// maps between node id and its broadcast cache
    std::unordered_map<h512, std::shared_ptr<PBFTMsgCache>> m_broadCastKeyCache;
};
}  // namespace consensus
}  // namespace dev
