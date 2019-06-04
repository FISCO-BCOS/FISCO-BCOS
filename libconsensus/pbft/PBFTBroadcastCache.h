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
/**
 * @brief : define classes to manager pbft back keys
 * @file: PBFTBroadcastCache.h
 * @author: yujiechen
 *
 * @date: 2019-06-04
 *
 */
#pragma once
#include "PBFTReqFactory.h"
#include <unordered_map>

namespace dev
{
namespace consensus
{
class PBFTBroadcastCache
{
public:
    PBFTBroadcastCache() {}
    virtual ~PBFTBroadcastCache() {}

    void setPBFTReqFactory(std::shared_ptr<PBFTReqFactory> pbftReqFactory)
    {
        m_pbftReqFactory = pbftReqFactory;
    }
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
            m_broadCastKeyCache[nodeId] = m_pbftReqFactory->buildPBFTMsgCache();
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
    std::shared_ptr<PBFTReqFactory> m_pbftReqFactory = nullptr;
};
}  // namespace consensus
}  // namespace dev