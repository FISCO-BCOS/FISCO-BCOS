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

using namespace bcos;
using namespace bcos::gateway;

void RouterTable::encode(bcos::bytes& _encodedData)
{
    WriteGuard l(x_routerEntries);
    m_inner()->routerEntries.clear();
    // encode m_routerEntries
    for (auto const& it : m_routerEntries)
    {
        auto entry = std::dynamic_pointer_cast<RouterTableEntry>(it.second);
        m_inner()->routerEntries.emplace_back(entry->inner());
    }
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    m_inner()->writeTo(output);
    output.getByteBuffer().swap(_encodedData);
}

void RouterTable::decode(bcos::bytesConstRef _decodedData)
{
    WriteGuard l(x_routerEntries);
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_decodedData.data(), _decodedData.size());
    m_inner()->readFrom(input);
    // decode into m_routerEntries
    m_routerEntries.clear();
    for (auto& it : m_inner()->routerEntries)
    {
        auto entry =
            std::make_shared<RouterTableEntry>([m_entry = it]() mutable { return &m_entry; });
        m_routerEntries.insert(std::make_pair(entry->dstNode(), entry));
    }
}

bool RouterTable::erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID)
{
    bool updated = false;
    WriteGuard l(x_routerEntries);
    // erase router-entry of the _p2pNodeID
    if (m_routerEntries.count(_p2pNodeID))
    {
        // Note: reset the distance to m_unreachableDistance, to notify that the _p2pNodeID is
        // unreachable
        auto entry = m_routerEntries.at(_p2pNodeID);
        entry->setDistance(m_unreachableDistance);
        entry->clearNextHop();
        _unreachableNodes.insert(entry->dstNode());

        SERVICE_ROUTER_LOG(INFO) << LOG_DESC("erase: make the router unreachable")
                                 << LOG_KV("dst", _p2pNodeID)
                                 << LOG_KV("distance", entry->distance())
                                 << LOG_KV("size", m_routerEntries.size());
        updated = true;
    }
    // update the router-entry with nextHop equal to _p2pNodeID to be unreachable
    updateDistanceForAllRouterEntries(_unreachableNodes, _p2pNodeID, m_unreachableDistance);
    return updated;
}

void RouterTable::updateDistanceForAllRouterEntries(
    std::set<std::string>& _unreachableNodes, std::string const& _nextHop, int32_t _newDistance)
{
    for (auto& it : m_routerEntries)
    {
        auto entry = it.second;
        if (entry->nextHop() == _nextHop)
        {
            auto oldDistance = entry->distance();
            entry->setDistance(_newDistance + 1);
            if (entry->distance() >= m_unreachableDistance)
            {
                entry->clearNextHop();
                _unreachableNodes.insert(entry->dstNode());
            }
            SERVICE_ROUTER_LOG(INFO)
                << LOG_DESC("update entry since the nextHop distance has been updated")
                << LOG_KV("dst", entry->dstNode()) << LOG_KV("nextHop", _nextHop)
                << LOG_KV("distance", entry->distance()) << LOG_KV("oldDistance", oldDistance)
                << LOG_KV("size", m_routerEntries.size());
        }
    }
}

bool RouterTable::update(std::set<std::string>& _unreachableNodes,
    std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry)
{
    SERVICE_ROUTER_LOG(TRACE) << LOG_DESC("RouterTable: receive entry")
                              << LOG_KV("dst", _entry->dstNode())
                              << LOG_KV("distance", _entry->distance())
                              << LOG_KV("from", _generatedFrom);
    auto ret = updateDstNodeEntry(_generatedFrom, _entry);
    // the dst entry has not been updated
    if (ret == false)
    {
        return false;
    }
    UpgradableGuard l(x_routerEntries);
    if (!m_routerEntries.count(_entry->dstNode()))
    {
        return false;
    }
    // get the latest distance
    auto currentEntry = m_routerEntries.at(_entry->dstNode());
    auto _newDistance = currentEntry->distance();
    if (_newDistance >= m_unreachableDistance)
    {
        currentEntry->clearNextHop();
        _unreachableNodes.insert(_entry->dstNode());
    }
    // the dst entry has updated, update the distance of the router-entries with nextHop equal to
    // dstNode
    UpgradeGuard ul(l);
    if (_newDistance == 1)
    {
        currentEntry->clearNextHop();
    }
    updateDistanceForAllRouterEntries(_unreachableNodes, _entry->dstNode(), _newDistance);
    return true;
}

bool RouterTable::updateDstNodeEntry(
    std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry)
{
    UpgradableGuard l(x_routerEntries);
    // the node self
    if (_entry->dstNode() == m_nodeID)
    {
        return false;
    }
    // insert new entry
    if (!m_routerEntries.count(_entry->dstNode()))
    {
        UpgradeGuard ul(l);
        _entry->incDistance(1);
        if (_generatedFrom != m_nodeID)
        {
            _entry->setNextHop(_generatedFrom);
        }
        m_routerEntries.insert(std::make_pair(_entry->dstNode(), _entry));
        SERVICE_ROUTER_LOG(INFO) << LOG_DESC(
                                        "updateDstNodeEntry: insert new entry into the routerTable")
                                 << LOG_KV("distance", _entry->distance())
                                 << LOG_KV("dst", _entry->dstNode())
                                 << LOG_KV("nextHop", _entry->nextHop())
                                 << LOG_KV("size", m_routerEntries.size());
        return true;
    }
    // discover smaller distance
    auto currentEntry = m_routerEntries.at(_entry->dstNode());
    auto currentDistance = currentEntry->distance();
    auto distance = _entry->distance() + 1;
    if (currentDistance > distance)
    {
        UpgradeGuard ul(l);
        if (_generatedFrom != m_nodeID)
        {
            currentEntry->setNextHop(_generatedFrom);
        }
        currentEntry->setDistance(distance);
        SERVICE_ROUTER_LOG(INFO)
            << LOG_DESC("updateDstNodeEntry: Discover smaller distance, update entry")
            << LOG_KV("distance", currentEntry->distance())
            << LOG_KV("oldDistance", currentDistance) << LOG_KV("dst", _entry->dstNode())
            << LOG_KV("nextHop", _entry->nextHop()) << LOG_KV("size", m_routerEntries.size());
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
        SERVICE_ROUTER_LOG(INFO) << LOG_DESC(
                                        "updateDstNodeEntry: distance of the nextHop Entry "
                                        "updated, update the current entry")
                                 << LOG_KV("dst", currentEntry->dstNode())
                                 << LOG_KV("nextHop", currentEntry->nextHop())
                                 << LOG_KV("distance", currentEntry->distance())
                                 << LOG_KV("size", m_routerEntries.size());
        return true;
    }
    return false;
}

std::string RouterTable::getNextHop(std::string const& _nodeID)
{
    std::string emptyNextHop;
    ReadGuard l(x_routerEntries);
    if (!m_routerEntries.count(_nodeID))
    {
        return emptyNextHop;
    }
    auto entry = m_routerEntries.at(_nodeID);
    if (entry->distance() >= m_unreachableDistance)
    {
        return emptyNextHop;
    }
    return entry->nextHop();
}

std::set<std::string> RouterTable::getAllReachableNode()
{
    std::set<std::string> reachableNodes;
    ReadGuard l(x_routerEntries);
    for (auto const& it : m_routerEntries)
    {
        auto entry = it.second;
        if (entry->distance() < m_unreachableDistance)
        {
            reachableNodes.insert(entry->dstNode());
        }
    }
    return reachableNodes;
}