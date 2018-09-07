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
/** @file Host.h
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "Common.h"
#include "HostCapabilityFace.h"
#include "Network.h"
#include "Peer.h"
#include "RLPXSocket.h"
#include <libdevcore/Guards.h>
#include <libdevcore/Worker.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/ECDHE.h>

namespace ba = boost::asio;
namespace bi = ba::ip;

namespace dev
{
namespace p2p
{
class RLPXHandshake;

class Host : public Worker
{
public:
    Host(std::string const& _clientVersion, NetworkConfig const& _n = NetworkConfig(),
        bytesConstRef _restoreNetwork = bytesConstRef());

    Host(std::string const& _clientVersion, KeyPair const& _alias,
        NetworkConfig const& _n = NetworkConfig());
    ~Host();

    template <class T>
    std::shared_ptr<T> registerCapability(std::shared_ptr<T> const& _t)
    {
        _t->m_host = this;
        m_capabilities[std::make_pair(T::staticName(), T::staticVersion())] = _t;
        return _t;
    }

    template <class T>
    void addCapability(std::shared_ptr<T> const& _p, std::string const& _name, u256 const& _version)
    {
        m_capabilities[std::make_pair(_name, _version)] = _p;
    }

    virtual bool haveCapability(CapDesc const& _name) const
    {
        return m_capabilities.count(_name) != 0;
    }

    virtual CapDescs caps() const
    {
        CapDescs ret;
        for (auto const& i : m_capabilities)
            ret.push_back(i.first);
        return ret;
    }

    template <class T>
    std::shared_ptr<T> cap() const
    {
        try
        {
            return std::static_pointer_cast<T>(
                m_capabilities.at(std::make_pair(T::staticName(), T::staticVersion())));
        }
        catch (...)
        {
            return nullptr;
        }
    }
    ///===========
    void start();
    void stop();
    void doneWorking();
    void startedWorking();
    void run(boost::system::error_code const& error);
    bool haveNetwork() const
    {
        Guard l(x_runTimer);
        return m_run;
    }
    ///===========

    ///-----Peer releated Informations
    size_t peerCount() const;
    PeerSessionInfos peerSessionInfo() const;

    bytes saveNetwork() const;

    std::string listenAddress() const
    {
        return m_tcpPublic.address().is_unspecified() ? "0.0.0.0" :
                                                        m_tcpPublic.address().to_string();
    }
    virtual unsigned short listenPort() const { return std::max(0, m_listenPort); }

    Peers getPeers() const
    {
        Peers ret;
        try
        {
            RecursiveGuard l(x_sessions);
            for (auto const& i : m_peers)
                ret.push_back(*i.second);
        }
        catch (...)
        {
        }

        return ret;
    }

    std::shared_ptr<SessionFace> havePeerSession(NodeID const& _id)
    {
        RecursiveGuard l(x_sessions);
        return m_sessions.count(_id) ? m_sessions[_id].lock() : std::shared_ptr<SessionFace>();
    }
    /// -----------------------------------
    NodeID id() const { return m_alias.pub(); }
    Secret sec() const { return m_alias.secret(); }
    KeyPair keyPair() const { return m_alias; }
    /// -----------------------------------

    void startPeerSession(RLP const& _rlp, std::shared_ptr<RLPXSocket> const& _s);
    /*void disconnectByNodeId(const std::string& sNodeId);*/


    ba::io_service* getIOService() { return &m_ioService; }
    std::unordered_map<NodeID, std::weak_ptr<SessionFace>>& mSessions() { return m_sessions; }
    RecursiveMutex& xSessions() { return x_sessions; }
    boost::asio::io_service::strand* getStrand() { return &m_strand; }
    std::string getClientVersion() const { return m_clientVersion; }
    std::map<NodeIPEndpoint, NodeID>* staticNodes() { return &_staticNodes; }
    void setStaticNodes(const std::map<NodeIPEndpoint, NodeID>& staticNodes)
    {
        _staticNodes = staticNodes;
    }
    bool isStarted() const { return isWorking(); }

    bi::tcp::endpoint const& tcpPublic() const { return m_tcpPublic; }
    NetworkConfig const& networkConfig() const { return m_netConfigs; }
    std::string enode() const
    {
        return "enode://" + id().hex() + "@" +
               (networkConfig().publicIPAddress.empty() ? m_tcpPublic.address().to_string() :
                                                          networkConfig().publicIPAddress) +
               ":" + toString(m_tcpPublic.port());
    }

    p2p::NodeInfo nodeInfo() const
    {
        return NodeInfo(id(),
            (networkConfig().publicIPAddress.empty() ? m_tcpPublic.address().to_string() :
                                                       networkConfig().publicIPAddress),
            m_tcpPublic.port(), m_clientVersion);
    }

    void reconnectNow()
    {
        Guard l(x_reconnectnow);
        m_reconnectnow = true;
    }
    void connect(NodeIPEndpoint const& _nodeIPEndpoint);
    // virtual std::function<bool(bool, boost::asio::ssl::verify_context&)>
    // newVerifyCallback(std::shared_ptr<std::string> nodeIDOut);
private:
    KeyPair networkAlias(bytesConstRef _b);
    void sslHandshakeServer(
        const boost::system::error_code& error, std::shared_ptr<RLPXSocket> socket);
    void sslHandshakeClient(const boost::system::error_code& error,
        std::shared_ptr<RLPXSocket> socket, NodeID id, NodeIPEndpoint& _nodeIPEndpoint);
    // bool sslVerifyCert(bool preverified, ba::ssl::verify_context& ctx);
    void determinePublic();

    void keepAlivePeers();
    // void reconnectAllNodes();
    // void disconnectLatePeers();
    void runAcceptor();

    unsigned peerSlots(PeerSlotType _type)
    {
        return _type == Egress ? m_idealPeerCount : m_idealPeerCount * m_stretchPeers;
    }
    bool peerSlotsAvailable(PeerSlotType _type = Ingress)
    {
        Guard l(x_pendingNodeConns);
        return peerCount() + m_pendingPeerConns.size() < peerSlots(_type);
    }

    void setIdealPeerCount(unsigned _n) { m_idealPeerCount = _n; }
    void setPeerStretch(unsigned _n) { m_stretchPeers = _n; }
    unsigned idealPeerCount() { return m_idealPeerCount; }
    unsigned stretchPeers() { return m_stretchPeers; }

private:
    Mutex x_timers;
    std::list<std::shared_ptr<boost::asio::deadline_timer>> m_timers;
    std::unique_ptr<boost::asio::deadline_timer> m_timer;
    bool m_accepting = false;

    std::string m_clientVersion;          ///< Our version string.
    NetworkConfig m_netConfigs;           ///< Network settings.
    std::set<bi::address> m_ifAddresses;  ///< Interface addresses.
    ba::io_service m_ioService;
    bi::tcp::acceptor m_tcp4Acceptor;  ///< Listening acceptor.
    KeyPair m_alias;  ///< Alias for network communication. Network address is k*G. k is key
                      ///< material. TODO: Replace KeyPair.
    std::chrono::steady_clock::time_point m_lastPing;  ///< Time we sent the last ping to all peers.
    std::chrono::steady_clock::time_point m_lastReconnect;  ///< Time we sent the last ping to all
                                                            ///< peers.
    boost::asio::io_service::strand m_strand;
    std::map<CapDesc, std::shared_ptr<HostCapabilityFace>> m_capabilities;  ///< Each of the
                                                                            ///< capabilities we
                                                                            ///< support.
    bytes m_restoreNetwork;  ///< Set by constructor and used to set Host key and restore network
                             ///< peers & nodes.
    bool m_run = false;
    mutable std::mutex x_runTimer;  ///< Start/stop mutex.

    mutable std::unordered_map<NodeID, std::weak_ptr<SessionFace>> m_sessions;
    mutable RecursiveMutex x_sessions;

    int m_listenPort = -1;  ///< What port are we listening on. -1 means binding failed or acceptor
                            ///< hasn't been initialized.
    std::set<NodeID> m_requiredPeers;
    std::map<NodeIPEndpoint, NodeID> _staticNodes;

    bi::tcp::endpoint m_tcpPublic;  ///< Our public listening endpoint.

    bool m_reconnectnow = true;

    std::set<std::string> m_pendingPeerConns;
    Mutex x_pendingNodeConns;
    std::unordered_map<std::string, std::shared_ptr<Peer>> m_peers;


    std::list<std::weak_ptr<RLPXHandshake>> m_connecting;
    Mutex x_connecting;
    Mutex x_reconnectnow;
    std::chrono::steady_clock::time_point m_lastAnnouncementConnectNodes;
    unsigned m_idealPeerCount = 128;  ///< Ideal number of peers to be connected to.
    unsigned m_stretchPeers = 7;  ///< Accepted connection multiplier (max peers = ideal*stretch).
    static const unsigned c_timerInterval = 100;
    bi::tcp::endpoint m_tcpClient;  // ip and port information of the connected peer
};
}  // namespace p2p

}  // namespace dev