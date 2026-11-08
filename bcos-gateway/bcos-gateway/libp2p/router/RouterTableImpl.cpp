/*
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
 * @file RouterTableImpl.cpp
 * @author: yujiechen
 * @date 2022-5-24
 */
#include "RouterTableImpl.h"
#include "../Common.h"
#include "bcos-tars-protocol/Common.h"
#include "bcos-utilities/LogStream.h"

using namespace bcos;
using namespace bcos::gateway;

RouterTableEntry::RouterTableEntry()
  : m_inner([m_entry = bcostars::RouterTableEntry()]() mutable { return &m_entry; })
{}

RouterTableEntry::RouterTableEntry(std::function<bcostars::RouterTableEntry*()> _inner)
  : m_inner(std::move(_inner))
{}

void RouterTableEntry::setDstNode(std::string const& _dstNode)
{
    m_inner()->dstNode = _dstNode;
}

void RouterTableEntry::setNextHop(std::string const& _nextHop)
{
    m_inner()->nextHop = _nextHop;
}

void RouterTableEntry::clearNextHop()
{
    m_inner()->nextHop = std::string();
}

void RouterTableEntry::setDistance(int32_t _distance)
{
    m_inner()->distance = _distance;
}

void RouterTableEntry::incDistance(int32_t _deltaDistance)
{
    m_inner()->distance += _deltaDistance;
}

std::string const& RouterTableEntry::dstNode() const
{
    return m_inner()->dstNode;
}

std::string const& RouterTableEntry::nextHop() const
{
    return m_inner()->nextHop;
}

int32_t RouterTableEntry::distance() const
{
    return m_inner()->distance;
}

bcostars::RouterTableEntry const& RouterTableEntry::inner() const
{
    return *(m_inner());
}

void RouterTableEntry::setDstNodeInfo(P2PInfo const& _dstNodeInfo)
{
    assignNodeIDInfo(m_inner()->dstNodeInfo, _dstNodeInfo);
}

void RouterTableEntry::resetDstNodeInfo(P2PInfo const& _dstNodeInfo)
{
    if (!m_inner()->dstNodeInfo.p2pID.empty())
    {
        return;
    }
    if (_dstNodeInfo.p2pID.empty())
    {
        return;
    }
    setDstNodeInfo(_dstNodeInfo);
}

P2PInfo RouterTableEntry::dstNodeInfo() const
{
    return {m_inner()->dstNodeInfo.p2pID, dstNode()};
}

RouterTable::RouterTable()
  : m_inner([m_table = bcostars::RouterTable()]() mutable { return &m_table; }),
    // entries own a tars object behind an accessor lambda, so a snapshot clone must rebuild them
    // instead of sharing the shared_ptrs with the previous snapshot
    m_entries(Entries{}, [](Entries const& entries) { return cloneEntries(entries); }),
    // a rebuild replaces the whole set, so cloning its contents would be pure waste: carry over the
    // bookkeeping only
    m_reachable(ReachableCache{},
        [](ReachableCache const& cached)
        {
            ReachableCache next;
            next.version = cached.version;
            next.valid = cached.valid;
            return next;
        })
{}

RouterTable::RouterTable(bytesConstRef _decodedData) : RouterTable()
{
    decode(_decodedData);
}

std::map<std::string, RouterTableEntryInterface::Ptr> const& RouterTable::routerEntries()
{
    // Returns the current snapshot's map. Callers must use it immediately: the snapshot is only
    // guaranteed alive for RcuSnapshot::gracePeriod() after it is superseded. Every in-tree caller
    // either iterates a freshly decoded table (never mutated concurrently) or only reads its size.
    return m_entries.read();
}

void RouterTable::setNodeID(std::string const& _nodeID)
{
    m_nodeID = _nodeID;
}

std::string const& RouterTable::nodeID() const
{
    return m_nodeID;
}

void RouterTable::setUnreachableDistance(int _unreachableDistance)
{
    m_unreachableDistance = _unreachableDistance;
}

RouterTableInterface::Ptr RouterTableFactoryImpl::createRouterTable()
{
    return std::make_shared<RouterTable>();
}

RouterTableInterface::Ptr RouterTableFactoryImpl::createRouterTable(
    bcos::bytesConstRef _decodedData)
{
    return std::make_shared<RouterTable>(_decodedData);
}

RouterTableEntryInterface::Ptr RouterTableFactoryImpl::createRouterEntry()
{
    return std::make_shared<RouterTableEntry>();
}

void RouterTable::encode(bcos::bytes& _encodedData)
{
    std::lock_guard encodeLock(x_encode);
    auto const& entries = m_entries.read();
    m_inner()->routerEntries.clear();
    // encode the current snapshot
    for (auto const& it : entries)
    {
        auto entry = std::dynamic_pointer_cast<RouterTableEntry>(it.second);
        m_inner()->routerEntries.emplace_back(entry->inner());
    }
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    m_inner()->writeTo(output);
    output.getByteBuffer().swap(_encodedData);
}

RouterTable::Entries RouterTable::cloneEntries(Entries const& entries)
{
    Entries copy;
    for (auto const& it : entries)
    {
        auto concrete = std::dynamic_pointer_cast<RouterTableEntry>(it.second);
        if (concrete)
        {
            copy.emplace(it.first, concrete->clone());
        }
        else
        {
            copy.emplace(it.first, it.second);
        }
    }
    return copy;
}

void RouterTable::decode(bcos::bytesConstRef _decodedData)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_decodedData.data(), _decodedData.size());

    Entries decoded;
    {
        std::lock_guard encodeLock(x_encode);
        m_inner()->readFrom(input);
        for (auto& it : m_inner()->routerEntries)
        {
            auto entry =
                std::make_shared<RouterTableEntry>([m_entry = it]() mutable { return &m_entry; });
            decoded.insert(std::make_pair(entry->dstNode(), entry));
        }
    }
    mutateEntries([&](Entries& entries) { entries = std::move(decoded); });
}

bool RouterTable::erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID)
{
    bool updated = false;
    // Snapshot update: readers keep seeing the previous table until the clone is published.
    mutateEntries([&](Entries& entries) {
        // erase router-entry of the _p2pNodeID
        auto it = entries.find(_p2pNodeID);
        if (it != entries.end())
        {
            // Note: reset the distance to m_unreachableDistance, to notify that the _p2pNodeID is
            // unreachable
            it->second->setDistance(m_unreachableDistance);
            it->second->clearNextHop();
            _unreachableNodes.insert(it->second->dstNode());

            SERVICE_ROUTER_LOG(INFO) << LOG_BADGE("erase") << LOG_DESC("make the router unreachable")
                                     << LOG_KV("dst", _p2pNodeID)
                                     << LOG_KV("distance", it->second->distance())
                                     << LOG_KV("size", entries.size());
            updated = true;
        }
        // update the router-entry with nextHop equal to _p2pNodeID to be unreachable
        updateDistanceForAllRouterEntries(
            _unreachableNodes, _p2pNodeID, m_unreachableDistance, entries);
    });
    return updated;
}

void RouterTable::updateDistanceForAllRouterEntries(
    std::set<std::string>& _unreachableNodes, std::string const& _nextHop, int32_t _newDistance)
{
    mutateEntries([&](Entries& entries) {
        updateDistanceForAllRouterEntries(_unreachableNodes, _nextHop, _newDistance, entries);
    });
}

void RouterTable::updateDistanceForAllRouterEntries(
    std::set<std::string>& _unreachableNodes, std::string const& _nextHop, int32_t _newDistance,
    Entries& entries)
{
    for (auto& it : entries)
    {
        auto entry = it.second;
        if (entry->nextHop() == _nextHop)
        {
            auto oldDistance = entry->distance();
            entry->setDistance(_newDistance + (oldDistance - 1));
            if (entry->distance() >= m_unreachableDistance)
            {
                entry->clearNextHop();
                _unreachableNodes.insert(entry->dstNode());
            }
            SERVICE_ROUTER_LOG(INFO)
                << LOG_BADGE("updateDistanceForAllRouterEntries")
                << LOG_DESC("update entry since the nextHop distance has been updated")
                << LOG_KV("dst", entry->printDstNode())
                << LOG_KV("nextHop", printShortP2pID(_nextHop))
                << LOG_KV("distance", entry->distance()) << LOG_KV("oldDistance", oldDistance)
                << LOG_KV("size", entries.size());
        }
    }
}

bool RouterTable::update(std::set<std::string>& _unreachableNodes,
    std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry)
{
    if (c_fileLogLevel <= TRACE) [[unlikely]]
    {
        SERVICE_ROUTER_LOG(TRACE) << LOG_BADGE("update") << LOG_DESC("receive entry")
                                  << LOG_KV("dst", printShortP2pID(_entry->dstNode()))
                                  << LOG_KV("distance", _entry->distance())
                                  << LOG_KV("from", printShortP2pID(_generatedFrom));
    }
    // One snapshot update covers both steps: inserting/refreshing the entry and then propagating
    // the new distance to every entry routed through it. Doing it in two publishes would expose an
    // intermediate table where the entry is fresh but its dependants still hold stale distances.
    bool ret = false;
    mutateEntries([&](Entries& entries) {
        ret = updateDstNodeEntry(entries, _generatedFrom, _entry);
        if (!ret)
        {
            return;
        }

        auto it = entries.find(_entry->dstNode());
        if (it == entries.end())
        {
            ret = false;
            return;
        }
        // get the latest distance
        auto& currentEntry = it->second;
        auto _newDistance = currentEntry->distance();
        if (_newDistance >= m_unreachableDistance)
        {
            currentEntry->clearNextHop();
            _unreachableNodes.insert(_entry->dstNode());
        }
        if (_newDistance == 1)
        {
            currentEntry->clearNextHop();
        }
        updateDistanceForAllRouterEntries(_unreachableNodes, _entry->dstNode(), _newDistance, entries);
    });
    return ret;
}

bool RouterTable::updateDstNodeEntry(
    std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry)
{
    bool ret = false;
    mutateEntries([&](Entries& entries) {
        ret = updateDstNodeEntry(entries, _generatedFrom, _entry);
    });
    return ret;
}

bool RouterTable::updateDstNodeEntry(
    Entries& entries, std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry)
{
    // the node self
    if (_entry->dstNode() == m_nodeID)
    {
        return false;
    }
    // insert new entry
    auto it = entries.find(_entry->dstNode());
    if (it == entries.end())
    {
        _entry->incDistance(1);
        if (_generatedFrom != m_nodeID)
        {
            _entry->setNextHop(_generatedFrom);
        }
        entries.emplace(_entry->dstNode(), _entry);
        SERVICE_ROUTER_LOG(INFO) << LOG_BADGE("updateDstNodeEntry")
                                 << LOG_DESC("insert new entry into the routerTable")
                                 << LOG_KV("distance", _entry->distance())
                                 << LOG_KV("dst", _entry->printDstNode())
                                 << LOG_KV("nextHop", _entry->printNextHop())
                                 << LOG_KV("size", entries.size());
        return true;
    }

    // discover smaller distance
    auto currentEntry = it->second;
    // try to reset the existed entry
    currentEntry->resetDstNodeInfo(_entry->dstNodeInfo());

    auto currentDistance = currentEntry->distance();
    auto distance = _entry->distance() + 1;
    if (currentDistance > distance)
    {
        if (_generatedFrom != m_nodeID)
        {
            currentEntry->setNextHop(_generatedFrom);
        }
        currentEntry->setDistance(distance);
        SERVICE_ROUTER_LOG(INFO) << LOG_BADGE("updateDstNodeEntry")
                                 << LOG_DESC("discover smaller distance, update entry")
                                 << LOG_KV("distance", currentEntry->distance())
                                 << LOG_KV("oldDistance", currentDistance)
                                 << LOG_KV("dst", _entry->printDstNode())
                                 << LOG_KV("nextHop", _entry->printNextHop())
                                 << LOG_KV("size", entries.size());
        return true;
    }
    // the distance information for the nextHop changed
    if (currentEntry->nextHop() == _generatedFrom)
    {
        // distance not updated
        if (currentEntry->distance() == distance)
        {
            return false;
        }
        // unreachable condition
        if (currentEntry->distance() >= m_unreachableDistance && distance >= m_unreachableDistance)
        {
            return false;
        }
        currentEntry->setDistance(distance);
        if (currentEntry->distance() >= m_unreachableDistance)
        {
            currentEntry->clearNextHop();
        }
        SERVICE_ROUTER_LOG(INFO) << LOG_BADGE("updateDstNodeEntry")
                                 << LOG_DESC(
                                        "distance of the nextHop entry "
                                        "updated, update the current entry")
                                 << LOG_KV("dst", currentEntry->printDstNode())
                                 << LOG_KV("nextHop", currentEntry->printNextHop())
                                 << LOG_KV("distance", currentEntry->distance())
                                 << LOG_KV("size", entries.size());
        return true;
    }
    return false;
}

std::string RouterTable::getNextHop(std::string const& _nodeID)
{
    // lock-free: one atomic load, then a map lookup on the immutable snapshot
    auto const& entries = m_entries.read();
    auto it = entries.find(_nodeID);
    if (it == entries.end())
    {
        return {};
    }
    if (it->second->distance() >= m_unreachableDistance)
    {
        return {};
    }
    return it->second->nextHop();
}

std::set<std::string> const* RouterTable::getAllReachableNode()
{
    // The entry table changes only on topology/membership events; a broadcast reads this on every
    // fan-out. Cache the result and rebuild it lazily, one build per change.
    //
    // m_entriesVersion doubles as the dirty flag: a cache whose version differs from the current
    // entry version is stale. The cache itself lives in an RcuSnapshot, so:
    //   - the common path is one atomic load plus a version compare, no lock;
    //   - a rebuild publishes a new cache via RcuSnapshot::update, and a reader holding the previous
    //     pointer keeps a valid set because the retired snapshot (and the set inside it) is only
    //     destroyed once its grace period expires.
    auto const currentVersion = m_entriesVersion.load(std::memory_order_acquire);

    auto const& cached = m_reachable.read();
    if (cached.valid && cached.version == currentVersion)
    {
        return &cached.nodes;
    }

    m_reachable.update([&](ReachableCache& cache) {
        // Another thread may have rebuilt it between our check and this publish; keep its result.
        if (cache.valid && cache.version == currentVersion)
        {
            return;
        }
        auto const& entries = m_entries.read();
        cache.nodes.clear();
        for (auto const& it : entries)
        {
            if (it.second->distance() < m_unreachableDistance)
            {
                cache.nodes.insert(it.second->dstNode());
            }
        }
        if (c_fileLogLevel <= LogLevel::TRACE) [[unlikely]]
        {
            SERVICE_ROUTER_LOG(TRACE) << LOG_BADGE("getAllReachableNode")
                                      << LOG_KV("nodes size", cache.nodes.size())
                                      << LOG_KV("version", currentVersion);
        }
        cache.version = currentVersion;
        cache.valid = true;
    });

    return &m_reachable.read().nodes;
}

bool RouterTable::isReachableNode(std::string const& _nodeID)
{
    // Direct lookup on the immutable snapshot: no set materialisation at all, which is what
    // ServiceV2::isReachable used to pay for every call.
    auto const& entries = m_entries.read();
    auto it = entries.find(_nodeID);
    return it != entries.end() && it->second->distance() < m_unreachableDistance;
}
