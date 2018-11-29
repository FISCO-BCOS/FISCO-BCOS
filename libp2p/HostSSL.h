/*
	This file is part of FISCO-BCOS.

	FISCO-BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO-BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file HostSSL.h
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

#include "Host.h"

namespace ba = boost::asio;
namespace bi = ba::ip;



namespace dev
{
	namespace p2p
	{
		class HostSSL: public HostApi
		{
		public:
			HostSSL(
					std::string const& _clientVersion,
					NetworkPreferences const& _n = NetworkPreferences(),
					bytesConstRef _restoreNetwork = bytesConstRef(),
					int const& _statisticsInterval = 10
			):HostApi(_clientVersion,_n,_restoreNetwork,_statisticsInterval)
			{
				m_lastAnnouncementConnectNodes=chrono::steady_clock::time_point::min();
			}

			HostSSL(
					std::string const& _clientVersion,
					KeyPair const& _alias,
					NetworkPreferences const& _n = NetworkPreferences(),
					int const& _statisticsInterval = 10
			):HostApi(_clientVersion,_alias,_n)
			{
				m_lastAnnouncementConnectNodes=chrono::steady_clock::time_point::min();
			}

			virtual ~HostSSL(){stop();}

			/// Add a potential peer.
			virtual void addPeer(NodeSpec const& _s, PeerType _t)override;
			virtual void addNode(NodeID const& _node, NodeIPEndpoint const& _endpoint)override;
			virtual void requirePeer(NodeID const& _node, NodeIPEndpoint const& _endpoint)override;
			virtual void requirePeer(NodeID const& _node, bi::address const& _addr, unsigned short _udpPort, unsigned short _tcpPort)override { requirePeer(_node, NodeIPEndpoint(_addr, _udpPort, _tcpPort)); }

			/// Note peer as no longer being required.
			virtual void relinquishPeer(NodeID const& _node)override;

			/// Get peer information.
			virtual PeerSessionInfos peerSessionInfo() const override;

			// TODO: P2P this should be combined with peers into a HostStat object of some kind; coalesce data, as it's only used for status information.
			virtual Peers getPeers() const override
			{
				Peers ret; 
				try{
					RecursiveGuard l(x_sessions); 
					for (auto const& i: m_peers) 
						ret.push_back(*i.second);
					
				}
				catch (...) {}
				
				return ret;
			}

			
			/// Validates and starts peer session, taking ownership of _io. Disconnects and returns false upon error.
			virtual void startPeerSession( RLP const& _hello, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocketSSL> const& _s, RLPBaseData &rlpBaseData) override;


			virtual void disconnectByNodeId(const std::string &sNodeId)override;

			//virtual void getAnnouncementNodeList(h256& _allPeerHash,std::vector<Node> & _nodes)override;

			virtual void onNodeTableEvent(NodeID const& _n, NodeTableEventType const& _e)override;
			virtual void connect(NodeIPEndpoint const& _nodeIPEndpoint)override;	
		
		private:
			void sslHandshakeServer(const boost::system::error_code& error,std::shared_ptr<RLPXSocketSSL> socket);
			void sslHandshakeClient(const boost::system::error_code& error,std::shared_ptr<RLPXSocketSSL> socket,NodeID id, NodeIPEndpoint& _nodeIPEndpoint);
			bool sslVerifyCert(bool preverified,ba::ssl::verify_context& ctx);


			unsigned peerSlots(PeerSlotType _type) { return _type == Egress ? m_idealPeerCount : m_idealPeerCount * m_stretchPeers; }
			bool havePeerSession(NodeID const& _id) { return !!peerSession(_id); }
			void determinePublic();
			
			bool peerSlotsAvailable(PeerSlotType _type = Ingress) { Guard l(x_pendingNodeConns); return peerCount() + m_pendingPeerConns.size() < peerSlots(_type); }
			void keepAlivePeers();
			void reconnectAllNodes();
			void disconnectLatePeers();
			void runAcceptor();

			virtual void startedWorking()override;
			virtual void run(boost::system::error_code const& error)override;                      
			virtual void doneWorking()override;

			void addConnParamsToNodeTable();


			std::set<std::string> m_pendingPeerConns;                                                    
			Mutex x_pendingNodeConns;
			std::unordered_map<std::string, std::shared_ptr<Peer>> m_peers;
			
			
			std::list<std::weak_ptr<RLPXHandshakeSSL>> m_connecting;                                  
			Mutex x_connecting;             
			std::chrono::steady_clock::time_point m_lastAnnouncementConnectNodes;                                                       
		};

	}
}
