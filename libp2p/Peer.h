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
/** @file Peer.h
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#pragma once

#include "Common.h"
namespace dev
{
namespace p2p
{
class Session;
class Node
{
public:
    Node() = default;
    Node(Node const&) = default;
    Node(Public _publicKey, NodeIPEndpoint const& _ip) : m_id(_publicKey), m_endpoint(_ip) {}
    Node(NodeSpec const& _s) : m_id(_s.id()), m_endpoint(_s.nodeIPEndpoint()) {}
    ///--------- get interfaces---------
    virtual NodeID const& address() const { return m_id; }
    virtual Public const& publicKey() const { return m_id; }
    virtual NodeID const& id() const { return m_id; }
    virtual NodeIPEndpoint& endpoint() { return m_endpoint; }
    virtual operator bool() const { return (bool)m_id; }
    ///--------- set interfaces---------
    virtual void setId(NodeID const& _id) { m_id = _id; };
    virtual void setEndpoint(NodeIPEndpoint const& _endpoint) { m_endpoint = _endpoint; }

protected:
    NodeID m_id;
    /// Endpoints by which we expect to reach node.
    NodeIPEndpoint m_endpoint;
};
/**
 * @brief Representation of connectivity state and all other pertinent Peer metadata.
 * A Peer represents connectivity between two nodes, which in this case, are the host
 * and remote nodes.
 *
 * State information necessary for loading network topology is maintained by NodeTable.
 *
 * @todo Implement 'bool required'
 * @todo Populate metadata upon construction; save when destroyed.
 * @todo Metadata for peers needs to be handled via a storage backend.
 * Specifically, peers can be utilized in a variety of
 * many-to-many relationships while also needing to modify shared instances of
 * those peers. Modifying these properties via a storage backend alleviates
 * Host of the responsibility. (&& remove save/restoreNetwork)
 * @todo reimplement recording of historical session information on per-transport basis
 * @todo move attributes into protected
 */
class Peer : public Node
{
    friend class Session;  /// Allows Session to update score and rating.

public:
    /// Construct Peer from Node.
    Peer(Node const& _node) : Node(_node) {}
    Peer(NodeID const& _node, NodeIPEndpoint const& _ip) : Node(_node, _ip) {}
    Peer(NodeSpec const& _s) : Node(_s) {}

    // bool isOffline() const { return !m_session.lock(); }
    virtual bool operator<(Peer const& _p) const;

    /// Return true if connection attempt should be made to this peer or false if
    bool shouldReconnect() const;

    /// Number of times connection has been attempted to peer.
    unsigned failedAttempts() const { return m_failedAttempts; }

    /// Reason peer was previously disconnected.
    DisconnectReason lastDisconnect() const { return m_lastDisconnect; }

    /// Peer session is noted as useful.
    void noteSessionGood() { m_failedAttempts = 0; }

    std::chrono::system_clock::time_point const& lastConnected() const { return m_lastConnected; }

    std::chrono::system_clock::time_point const& lastAttempted() const { return m_lastAttempted; }

    void setLastConnected(std::chrono::system_clock::time_point const& _lastConnected)
    {
        m_lastConnected = _lastConnected;
    }

    void setLastAttempted(std::chrono::system_clock::time_point const& _lastAttempted)
    {
        m_lastAttempted = _lastAttempted;
    }

    void setfailedAttempts(unsigned const& _failedAttempts) { m_failedAttempts = _failedAttempts; }

    void setLastDisconnect(DisconnectReason const& reason) { m_lastDisconnect = reason; }

protected:
    /// Returns number of seconds to wait until attempting connection, based on attempted connection
    /// history.
    unsigned fallbackSeconds() const;
    DisconnectReason m_lastDisconnect =
        NoDisconnect;  ///< Reason for disconnect that happened last.

    /// Used by isOffline() and (todo) for peer to emit session information.
    // std::weak_ptr<Session> m_session;

private:
    std::chrono::system_clock::time_point m_lastConnected;
    std::chrono::system_clock::time_point m_lastAttempted;
    /// Network Availability
    unsigned m_failedAttempts = 0;
};
using Peers = std::vector<Peer>;
}  // namespace p2p
}  // namespace dev
