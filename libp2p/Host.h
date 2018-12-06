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

#include <mutex>
#include <map>
#include <vector>
#include <set>
#include <memory>
#include <utility>
#include <thread>
#include <chrono>

#include <libdevcore/Guards.h>
#include <libdevcore/Worker.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/ECDHE.h>
#include "NodeTable.h"
#include "HostCapability.h"
#include "Network.h"
#include "Peer.h"
#include "RLPXSocket.h"
#include "RLPXSocketSSL.h"
#include "RLPXFrameCoder.h"
#include "HandshakeCAData.h"
#include "Common.h"
namespace ba = boost::asio;
namespace bi = ba::ip;

namespace std
{
	template<> struct hash<pair<dev::p2p::NodeID, string>>
{
	size_t operator()(pair<dev::p2p::NodeID, string> const& _value) const
	{
		size_t ret = hash<dev::p2p::NodeID>()(_value.first);
		return ret ^ (hash<string>()(_value.second) + 0x9e3779b9 + (ret << 6) + (ret >> 2));
	}
};
}

namespace dev
{

	namespace p2p
	{

		class HostApi;

		class HostNodeTableHandler: public NodeTableEventHandler
		{
		public:
			HostNodeTableHandler(HostApi& _host);

			HostApi const& host() const { return m_host; }

		private:
			virtual void processEvent(NodeID const& _n, NodeTableEventType const& _e);

			HostApi& m_host;
		};

		struct SubReputation
		{
			bool isRude = false;
			int utility = 0;
			bytes data;
		};

		struct Reputation
		{
			std::unordered_map<std::string, SubReputation> subs;
		};

		class ReputationManager
		{
		public:
			ReputationManager();

			void noteRude(SessionFace const& _s, std::string const& _sub = std::string());
			bool isRude(SessionFace const& _s, std::string const& _sub = std::string()) const;
			void setData(SessionFace const& _s, std::string const& _sub, bytes const& _data);
			bytes data(SessionFace const& _s, std::string const& _subs) const;

		private:
			std::unordered_map<std::pair<p2p::NodeID, std::string>, Reputation> m_nodes;    ///< Nodes that were impolite while syncing. We avoid syncing from these if possible.
			SharedMutex mutable x_nodes;
		};

		struct NodeInfo
		{
			NodeInfo() = default;
			NodeInfo(NodeID const& _id, std::string const& _address, unsigned _port, std::string const& _version):
					id(_id), address(_address), port(_port), version(_version) {}

			std::string enode() const { return "enode://" + id.hex() + "@" + address + ":" + toString(port); }

			NodeID id;
			std::string address;
			unsigned port;
			std::string version;
		};

		enum PeerSlotType { Egress, Ingress };

		class HostApi: public Worker, public std::enable_shared_from_this<HostApi>
		{
			

		public:
			friend class HostNodeTableHandler;
			friend class RLPXHandshake;
			friend class RLPXHandshakeSSL;

			friend class Session;
			friend class HostCapabilityFace;
			template <class T> friend class HostCapability;
			friend class EthereumHost;
			friend class PBFTHost;

			HostApi(
					std::string const& _clientVersion,
					NetworkPreferences const& _n = NetworkPreferences(),
					bytesConstRef _restoreNetwork = bytesConstRef(),
					int const& _statisticsInterval = 10
			);

			HostApi(
					std::string const& _clientVersion,
					KeyPair const& _alias,
					NetworkPreferences const& _n = NetworkPreferences(),
					int const& _statisticsInterval = 10
			);

			virtual ~HostApi();

			template <class T> 
			std::shared_ptr<T> registerCapability(std::shared_ptr<T> const& _t)
			{
				_t->m_host = shared_from_this();
				m_capabilities[std::make_pair(T::staticName(), T::staticVersion())] = _t;
				return _t;
			}
			template <class T> 
			void addCapability(std::shared_ptr<T> const & _p, std::string const& _name, u256 const& _version)
			{ m_capabilities[std::make_pair(_name, _version)] = _p; }
			

			virtual bool haveCapability(CapDesc const& _name) const { return m_capabilities.count(_name) != 0; }
			virtual CapDescs caps() const { CapDescs ret; for (auto const& i: m_capabilities) ret.push_back(i.first); return ret; }
			template <class T> std::shared_ptr<T> cap() const { try { return std::static_pointer_cast<T>(m_capabilities.at(std::make_pair(T::staticName(), T::staticVersion()))); } catch (...) { return nullptr; } }

			virtual void addPeer(NodeSpec const& _s, PeerType _t){}
			virtual void addNode(NodeID const& _node, NodeIPEndpoint const& _endpoint){}
			virtual void requirePeer(NodeID const& _node, NodeIPEndpoint const& _endpoint){}
			virtual void requirePeer(NodeID const& _node, bi::address const& _addr, unsigned short _udpPort, unsigned short _tcpPort) { requirePeer(_node, NodeIPEndpoint(_addr, _udpPort, _tcpPort)); }
			virtual void relinquishPeer(NodeID const& _node){}
			virtual void setIdealPeerCount(unsigned _n) { m_idealPeerCount = _n; }
			virtual void setPeerStretch(unsigned _n) { m_stretchPeers = _n; }
			virtual ba::io_service* getIOService() { return &m_ioService; }

			/// Get peer information.
			virtual PeerSessionInfos peerSessionInfo()const ;
			virtual size_t peerCount() const;
			virtual std::string listenAddress() const { return m_tcpPublic.address().is_unspecified() ? "0.0.0.0" : m_tcpPublic.address().to_string(); }
			virtual unsigned short listenPort() const { return std::max(0, m_listenPort); }
			virtual bytes saveNetwork() const;

			
			virtual Peers getPeers() const { RecursiveGuard l(x_sessions); Peers ret;  return ret; }
			virtual NetworkPreferences const& networkPreferences() const { return m_netPrefs; }
			virtual void setNetworkPreferences(NetworkPreferences const& _p, bool _dropPeers = false) { m_dropPeers = _dropPeers; auto had = isStarted(); if (had) stop(); m_netPrefs = _p; if (had) start(); }
			virtual bool haveNetwork() const { Guard l(x_runTimer); return m_run && !!m_nodeTable; }

			virtual void startPeerSession(Public const& _id, RLP const& _hello, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocket> const& _s, RLPBaseData &rlpBaseData){}
			virtual void startPeerSession( RLP const& _hello, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocketSSL> const& _s, RLPBaseData &rlpBaseData){}

			void start();
			void stop();
			bool isStarted() const { return isWorking(); }

			ReputationManager& repMan() { return m_repMan; }
			std::shared_ptr<SessionFace> peerSession(NodeID const& _id) { RecursiveGuard l(x_sessions); return m_sessions.count(_id) ? m_sessions[_id].lock() : std::shared_ptr<SessionFace>(); }
			NodeID id() const { return m_alias.pub(); }
			Secret sec() const { return m_alias.sec(); }
			KeyPair keyPair() const { return m_alias; }
			bi::tcp::endpoint const& tcpPublic() const { return m_tcpPublic; }
			std::string enode() const { return "enode://" + id().hex() + "@" + (networkPreferences().publicIPAddress.empty() ? m_tcpPublic.address().to_string() : networkPreferences().publicIPAddress) + ":" + toString(m_tcpPublic.port()); }
			p2p::NodeInfo nodeInfo() const { return NodeInfo(id(), (networkPreferences().publicIPAddress.empty() ? m_tcpPublic.address().to_string() : networkPreferences().publicIPAddress), m_tcpPublic.port(), m_clientVersion); }

			virtual bool getSelfSignData(Signature &_sign) const{return false;}
			virtual void disconnectByNodeId(const std::string &sNodeId){}
			virtual void disconnectByPub256(const std::string &pub256){}
			virtual void recheckCAByPub256(const std::string &pub256){}
			virtual void recheckAllCA(){}
			virtual void saveCADataByNodeId(const std::string nodeId, CABaseData &baseData){}

			boost::asio::io_service::strand* getStrand()
			{
				return &m_strand;
			}
			std::string getClientVersion()const
			{
				return m_clientVersion;
			}
			void reconnectNow()
			{
				Guard l(x_reconnectnow);
				m_reconnectnow=true;
			}
			RecursiveMutex& xSessions()
			{
				return x_sessions;
			} 
			std::unordered_map<NodeID, std::weak_ptr<SessionFace>>& mSessions()
			{
				return  m_sessions;
			}

			virtual void onNodeTableEvent(NodeID const& _n, NodeTableEventType const& _e){}
			virtual void getAnnouncementNodeList(h256& _allPeerHash,std::vector<Node> & _nodes){}
			virtual void connect(NodeIPEndpoint const& _nodeIPEndpoint){}	

		protected:
			/// Called by Worker. Not thread-safe; to be called only by worker.
			virtual void startedWorking(){}
			virtual void run(boost::system::error_code const& error){}   
			virtual void doWork();
			virtual void doneWorking(){};

			/// Get or create host identifier (KeyPair).
			static KeyPair networkAlias(bytesConstRef _b);

			bytes m_restoreNetwork;                                                                 ///< Set by constructor and used to set Host key and restore network peers & nodes.
			bool m_run = false; 
			mutable std::mutex x_runTimer;  ///< Start/stop mutex.
			std::string m_clientVersion;                                                            ///< Our version string.
			NetworkPreferences m_netPrefs;                                                          ///< Network settings.
			std::set<bi::address> m_ifAddresses;                                                    ///< Interface addresses.
			int m_listenPort = -1;                                                                  ///< What port are we listening on. -1 means binding failed or acceptor hasn't been initialized.
			int m_statisticsInterval = 10;
			ba::io_service m_ioService;                                                             ///< IOService for network stuff.
			bi::tcp::acceptor m_tcp4Acceptor;                                                       ///< Listening acceptor.
			std::unique_ptr<boost::asio::deadline_timer> m_timer;                                   ///< Timer which, when network is running, calls scheduler() every c_timerInterval ms.
			static const unsigned c_timerInterval = 100;                                            ///< Interval which m_timer is run when network is connected.

			bi::tcp::endpoint m_tcpPublic;                                                          ///< Our public listening endpoint.
			bi::tcp::endpoint m_tcpClient;// ip and port information of the connected peer
			KeyPair m_alias;                                                                        ///< Alias for network communication. Network address is k*G. k is key material. TODO: Replace KeyPair.
			std::shared_ptr<NodeTable> m_nodeTable;                                                 ///< Node table (uses kademlia-like discovery).
			std::set<NodeID> m_requiredPeers;
			Mutex x_requiredPeers;
			mutable std::unordered_map<NodeID, std::weak_ptr<SessionFace>> m_sessions;
			mutable RecursiveMutex x_sessions;
			bool m_reconnectnow=true;
			unsigned m_idealPeerCount = 128;                                                         ///< Ideal number of peers to be connected to.
			unsigned m_stretchPeers = 7;                                                            ///< Accepted connection multiplier (max peers = ideal*stretch).
			std::map<CapDesc, std::shared_ptr<HostCapabilityFace>> m_capabilities;  ///< Each of the capabilities we support.
			std::list<std::shared_ptr<boost::asio::deadline_timer>> m_timers;
			Mutex x_timers;
			std::chrono::steady_clock::time_point m_lastPing;                                       ///< Time we sent the last ping to all peers.
			std::chrono::steady_clock::time_point m_lastReconnect;                                  ///< Time we sent the last ping to all peers.
			bool m_accepting = false;
			bool m_dropPeers = false;
			Mutex x_reconnectnow;
			ReputationManager m_repMan;
			boost::asio::io_service::strand m_strand;
		};

		class Host: public HostApi
		{
			
		public:
			/// Start server, listening for connections on the given port.
			Host(
					std::string const& _clientVersion,
					NetworkPreferences const& _n = NetworkPreferences(),
					bytesConstRef _restoreNetwork = bytesConstRef(),
					int const& _statisticsInterval = 10
			):HostApi(_clientVersion,_n,_restoreNetwork,_statisticsInterval){}

			
			Host(
					std::string const& _clientVersion,
					KeyPair const& _alias,
					NetworkPreferences const& _n = NetworkPreferences(),
					int const& _statisticsInterval = 10
			):HostApi(_clientVersion,_alias,_n,_statisticsInterval){}

			/// Will block on network process events.
			virtual ~Host(){}

			/// Add a potential peer.
			virtual void addPeer(NodeSpec const& _s, PeerType _t)override;
			/// Add node as a peer candidate. Node is added if discovery ping is successful and table has capacity.
			virtual void addNode(NodeID const& _node, NodeIPEndpoint const& _endpoint)override;
			/// Create Peer and attempt keeping peer connected.
			virtual void requirePeer(NodeID const& _node, NodeIPEndpoint const& _endpoint)override;
			/// Note peer as no longer being required.
			virtual void relinquishPeer(NodeID const& _node)override;

			virtual Peers getPeers() const override { RecursiveGuard l(x_sessions); Peers ret; for (auto const& i: m_peers) ret.push_back(*i.second); return ret; }
			/// Validates and starts peer session, taking ownership of _io. Disconnects and returns false upon error.
			virtual void startPeerSession(Public const& _id, RLP const& _hello, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocket> const& _s, RLPBaseData &rlpBaseData)override;
			//virtual void startPeerSession( RLP const& _hello, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocketSSL> const& _s, RLPBaseData &rlpBaseData)override {}
		
			virtual void disconnectByNodeId(const std::string &sNodeId) override;
			virtual void disconnectByPub256(const std::string &pub256) override;
			virtual void recheckCAByPub256(const std::string &pub256) override;
			virtual void recheckAllCA() override;
			virtual void saveCADataByNodeId(const std::string nodeId, CABaseData &baseData) override;

	
			virtual void onNodeTableEvent(NodeID const& _n, NodeTableEventType const& _e) override;
			void restoreNetwork(bytesConstRef _b);

		private:
			void sslHandshakeServer(const boost::system::error_code& error,std::shared_ptr<RLPXSocket> socket);
			void sslHandshakeClient(const boost::system::error_code& error,std::shared_ptr<RLPXSocket> socket,NodeID id,Peer* nptr);
			bool sslVerifyCert(bool preverified,ba::ssl::verify_context& ctx);

			unsigned peerSlots(PeerSlotType _type) { return _type == Egress ? m_idealPeerCount : m_idealPeerCount * m_stretchPeers; }
			bool havePeerSession(NodeID const& _id) { return !!peerSession(_id); }
			void determinePublic();
			void connect(std::shared_ptr<Peer> const& _p);
			bool peerSlotsAvailable(PeerSlotType _type = Ingress) { Guard l(x_pendingNodeConns); return peerCount() + m_pendingPeerConns.size() < peerSlots(_type); }

			void keepAlivePeers();
			void reconnectAllNodes();
			void disconnectLatePeers();
			void runAcceptor();

			virtual void startedWorking() override;
			virtual void run(boost::system::error_code const& error)override;   
			virtual void doneWorking()override;

			void addConnParamsToNodeTable();
                                     
			std::set<Peer*> m_pendingPeerConns;                                                    
			Mutex x_pendingNodeConns;
			std::unordered_map<NodeID, std::shared_ptr<Peer>> m_peers;
			std::list<std::weak_ptr<RLPXHandshake>> m_connecting;                                   
			Mutex x_connecting;                                                                                                 
		};

		class HostSingleton{
			public:
				static HostApi* GetHost(
					std::string const& _clientVersion,
					NetworkPreferences const& _n = NetworkPreferences(),
					bytesConstRef _restoreNetwork = bytesConstRef(),
					int const& _statisticsInterval = 10
				);
		};
		
	}
}
