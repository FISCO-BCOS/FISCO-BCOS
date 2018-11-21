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
/** @file Host.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
*  @author toxotguo
 * @date 2018
 */

#include <set>
#include <chrono>
#include <thread>
#include <mutex>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <libdevcore/Assertions.h>
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcore/CommonIO.h>
#include <libethcore/CommonJS.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FileSystem.h>
#include <libethereum/SystemContract.h>
#include <libdevcrypto/Rsa.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include "Session.h"
#include "Common.h"
#include "Capability.h"
#include "UPnP.h"
#include "RLPxHandshake.h"
#include "SessionWBCAData.h"
#include "Host.h"
#include "HostSSL.h"
#include "ParseCert.h"
using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;
using namespace dev::crypto;


/// Interval at which Host::run will call keepAlivePeers to ping peers.
std::chrono::seconds const c_keepAliveInterval = std::chrono::seconds(30);
std::chrono::seconds const c_reconnectNodesInterval = std::chrono::seconds(60);

/// Disconnect timeout after failure to respond to keepAlivePeers ping.
std::chrono::milliseconds const c_keepAliveTimeOut = std::chrono::milliseconds(1000);

HostNodeTableHandler::HostNodeTableHandler(HostApi& _host): m_host(_host) {}

void HostNodeTableHandler::processEvent(NodeID const& _n, NodeTableEventType const& _e)
{
	m_host.onNodeTableEvent(_n, _e);
}

ReputationManager::ReputationManager()
{
}

void ReputationManager::noteRude(SessionFace const& _s, std::string const& _sub)
{
	DEV_WRITE_GUARDED(x_nodes)
	m_nodes[make_pair(_s.id(), _s.info().clientVersion)].subs[_sub].isRude = true;
}

bool ReputationManager::isRude(SessionFace const& _s, std::string const& _sub) const
{
	DEV_READ_GUARDED(x_nodes)
	{
		auto nit = m_nodes.find(make_pair(_s.id(), _s.info().clientVersion));
		if (nit == m_nodes.end())
			return false;
		auto sit = nit->second.subs.find(_sub);
		bool ret = sit == nit->second.subs.end() ? false : sit->second.isRude;
		return _sub.empty() ? ret : (ret || isRude(_s));
	}
	return false;
}

void ReputationManager::setData(SessionFace const& _s, std::string const& _sub, bytes const& _data)
{
	DEV_WRITE_GUARDED(x_nodes)
	m_nodes[make_pair(_s.id(), _s.info().clientVersion)].subs[_sub].data = _data;
}

bytes ReputationManager::data(SessionFace const& _s, std::string const& _sub) const
{
	DEV_READ_GUARDED(x_nodes)
	{
		auto nit = m_nodes.find(make_pair(_s.id(), _s.info().clientVersion));
		if (nit == m_nodes.end())
			return bytes();
		auto sit = nit->second.subs.find(_sub);
		return sit == nit->second.subs.end() ? bytes() : sit->second.data;
	}
	return bytes();
}

HostApi::HostApi(string const& _clientVersion, KeyPair const& _alias, NetworkPreferences const& _n,int const& _statsInterval):
	Worker("p2p", 0),
	m_clientVersion(_clientVersion),
	m_netPrefs(_n),
	m_ifAddresses(Network::getInterfaceAddresses()),
	m_ioService(1),
	m_tcp4Acceptor(m_ioService),
	m_alias(_alias),
	m_lastPing(chrono::steady_clock::time_point::min()),
	m_lastReconnect(chrono::steady_clock::time_point::min()),
	m_strand(m_ioService)
{
	m_statisticsInterval = _statsInterval;
	LOG(INFO) << "Id:" << id().abridged();
}

HostApi::HostApi(string const& _clientVersion, NetworkPreferences const& _n, bytesConstRef _restoreNetwork, int const& _statsInterval):
	HostApi(_clientVersion, networkAlias(_restoreNetwork), _n,_statsInterval)
{
	
	m_restoreNetwork = _restoreNetwork.toBytes();
}

HostApi::~HostApi()
{
	stop();
}

void HostApi::start()
{
	DEV_TIMED_FUNCTION_ABOVE(500);
	startWorking();//启动work循环
	while (isWorking() && !haveNetwork())
		this_thread::sleep_for(chrono::milliseconds(10));

	// network start failed!
	if (isWorking())
		return;

	LOG(ERROR) << "Network start failed!";
	doneWorking();
}

void HostApi::stop()
{
	// called to force io_service to kill any remaining tasks it might have -
	// such tasks may involve socket reads from Capabilities that maintain references
	// to resources we're about to free.

	{
		// Although m_run is set by stop() or start(), it effects m_runTimer so x_runTimer is used instead of a mutex for m_run.
		Guard l(x_runTimer);
		// ignore if already stopped/stopping
		if (!m_run)
			return;

		// signal run() to prepare for shutdown and reset m_timer
		m_run = false;
	}

	// wait for m_timer to reset (indicating network scheduler has stopped)
	while (!!m_timer)
		this_thread::sleep_for(chrono::milliseconds(50));

	// stop worker thread
	if (isWorking())
		stopWorking();
	// stop capabilities (eth: stops syncing or block/tx broadcast)
	for (auto const& h : m_capabilities)
		h.second->onStopping();
}

void HostApi::doWork()
{
	try
	{
		if (m_run)
			m_ioService.run();
	}
	catch (std::exception const& _e)
	{
		LOG(WARNING) << "Exception in Network Thread:" << _e.what();
		LOG(WARNING) << "Network Restart is Recommended.";
	}

	if(m_ioService.stopped()) {
		m_ioService.reset();
	}
}

PeerSessionInfos HostApi::peerSessionInfo() const
{
	if (!m_run)
		return PeerSessionInfos();

	std::vector<PeerSessionInfo> ret;
	RecursiveGuard l(x_sessions);
	for (auto& i : m_sessions)
		if (auto j = i.second.lock())
			if (j->isConnected())
				ret.push_back(j->info());
	return ret;
}
size_t HostApi::peerCount() const
{
	unsigned retCount = 0;
	RecursiveGuard l(x_sessions);
	for (auto& i : m_sessions)
		if (std::shared_ptr<SessionFace> j = i.second.lock())
			if (j->isConnected())
				retCount++;
	return retCount;
}
bytes HostApi::saveNetwork() const
{

	RLPStream ret(3);
	ret << dev::p2p::c_protocolVersion << m_alias.secret().ref();
	int count = 0;
	ret.appendList(count);
	/*if (!!count)
		ret.appendRaw(network.out(), count);*/
	return ret.out();
}
KeyPair HostApi::networkAlias(bytesConstRef _b)
{
	RLP r(_b);
	if (r.itemCount() == 3 && r[0].isInt() && r[0].toInt<unsigned>() >= 3)
		return KeyPair(Secret(r[1].toBytes()));
	else
	{
		KeyPair kp = KeyPair::create();
		RLPStream netData(3);
		netData << dev::p2p::c_protocolVersion << kp.secret().ref();
		int count = 0;
		netData.appendList(count);

		writeFile(getDataDir() + "/network.rlp", netData.out());
		return kp;
	}
}


void Host::startPeerSession(Public const& _id, RLP const& _rlp, unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocket> const& _s, RLPBaseData &/*rlpBaseData*/)
{
	// session maybe ingress or egress so m_peers and node table entries may not exist
	shared_ptr<Peer> p;
	DEV_RECURSIVE_GUARDED(x_sessions)
	{
		if (m_peers.count(_id))
			p = m_peers[_id];
		else
		{
			// peer doesn't exist, try to get port info from node table
			if (m_nodeTable)
				if (Node n = m_nodeTable->node(_id))
					p = make_shared<Peer>(n);

			if (!p)
				p = make_shared<Peer>(Node(_id, UnspecifiedNodeIPEndpoint));

			m_peers[_id] = p;
		}
	}
	if (p->isOffline())
		p->m_lastConnected = std::chrono::system_clock::now();
	p->endpoint.address = _s->remoteEndpoint().address();

	auto protocolVersion = _rlp[0].toInt<unsigned>();
	auto clientVersion = _rlp[1].toString();
	auto caps = _rlp[2].toVector<CapDesc>();//通信信道
	auto listenPort = _rlp[3].toInt<unsigned short>();
	auto pub = _rlp[4].toHash<Public>();
	if (pub != _id)
	{
		LOG(DEBUG) << "Wrong ID: " << pub.abridged() << " vs. " << _id.abridged();
		return;
	}

	// clang error (previously: ... << hex << caps ...)
	// "'operator<<' should be declared prior to the call site or in an associated namespace of one of its arguments"
	stringstream capslog;

	// leave only highset mutually supported capability version
	caps.erase(remove_if(caps.begin(), caps.end(), [&](CapDesc const & _r) { return !haveCapability(_r) || any_of(caps.begin(), caps.end(), [&](CapDesc const & _o) { return _r.first == _o.first && _o.second > _r.second && haveCapability(_o); }); }), caps.end());

	for (auto cap : caps)
		capslog << "(" << cap.first << "," << dec << cap.second << ")";

	LOG(INFO) << "Hello: " << clientVersion << "V[" << protocolVersion << "]" << _id << showbase << capslog.str() << dec << listenPort;

	// create session so disconnects are managed
	shared_ptr<SessionFace> ps = make_shared<Session>(this, move(_io), _s, p, PeerSessionInfo({_id, clientVersion, p->endpoint.address.to_string(), listenPort, chrono::steady_clock::duration(), _rlp[2].toSet<CapDesc>(), 0, map<string, string>(), protocolVersion}));
	// ((Session *)ps.get())->setStatistics(new InterfaceStatistics(getDataDir() + "P2P" + p->id.hex(), m_statisticsInterval));

	if (protocolVersion < dev::p2p::c_protocolVersion - 1)
	{
		ps->disconnect(IncompatibleProtocol);
		return;
	}
	if (caps.empty())
	{
		ps->disconnect(UselessPeer);
		return;
	}

	if (m_netPrefs.pin && !m_requiredPeers.count(_id))
	{
		LOG(DEBUG) << "Unexpected identity from peer (got" << _id.abridged() << ", must be one of " << m_requiredPeers << ")";
		ps->disconnect(UnexpectedIdentity);
		return;
	}

	// WBCAData wbcaData;
	// bool ok = NodeConnManagerSingleton::GetInstance().CheckAndSerialize(_rlp, rlpBaseData, wbcaData);
	//replace CA check with hash check
	auto nodeInfoHash = _rlp[5].toHash<h256>();
	bool ok = NodeConnManagerSingleton::GetInstance().checkNodeInfo(pub.hex(), nodeInfoHash);
	if (!ok)
	{
		ps->disconnect(UnexpectedIdentity);
		return;
	}

	{
		RecursiveGuard l(x_sessions);
		if (m_sessions.count(_id) && !!m_sessions[_id].lock())
			if (auto s = m_sessions[_id].lock())
				if (s->isConnected())
				{
					// Already connected.
					LOG(INFO) << "Session already exists for peer with id" << _id.abridged();
					ps->disconnect(DuplicatePeer);
					return;
				}

		if (!peerSlotsAvailable())
		{
			ps->disconnect(TooManyPeers);
			return;
		}

		unsigned offset = (unsigned)UserPacket;
		uint16_t cnt = 1;

		// todo: mutex Session::m_capabilities and move for(:caps) out of mutex.
		for (auto const& i : caps)
		{
			auto pcap = m_capabilities[i];
			if (!pcap)
				return ps->disconnect(IncompatibleProtocol);

			if (Session::isFramingAllowedForVersion(protocolVersion))
				pcap->newPeerCapability(ps, 0, i, cnt++);
			else
			{
				pcap->newPeerCapability(ps, offset, i, 0);
				//will callback newPeerCapability && 
				//callback 'init lim' of EthereumPeer to send StatusPacket package
				offset += pcap->messageCount();
			}
		}

		ps->start();//start session by sending ping package
		m_sessions[_id] = ps;
	}

	LOG(INFO) << "p2p.host.peer.register: " << _id.abridged();
}

void Host::onNodeTableEvent(NodeID const& _n, NodeTableEventType const& _e)
{
	if (_e == NodeEntryAdded)
	{
		LOG(INFO) << "p2p.host.nodeTable.events.nodeEntryAdded: " << _n.abridged();
		// only add iff node is in node table
		if (Node n = m_nodeTable->node(_n))
		{
			shared_ptr<Peer> p;
			DEV_RECURSIVE_GUARDED(x_sessions)
			{
				if (m_peers.count(_n))
				{
					p = m_peers[_n];
					p->endpoint = n.endpoint;
				}
				else
				{
					p = make_shared<Peer>(n);
					m_peers[_n] = p;
					LOG(INFO) << "p2p.host.peers.events.peerAdded: " << _n.abridged() << p->endpoint;
				}
			}
			if (peerSlotsAvailable(Egress))
				connect(p);
			else 
			{
				LOG(INFO)<<" too many egress peer!";
			}
		}
	}
	else if (_e == NodeEntryDropped)
	{
		LOG(INFO) << "p2p.host.nodeTable.events.NodeEntryDropped: " << _n.abridged();
		RecursiveGuard l(x_sessions);
		if (m_peers.count(_n) && m_peers[_n]->peerType == PeerType::Optional)
			m_peers.erase(_n);
	}
}

void Host::determinePublic()
{
	// set m_tcpPublic := listenIP (if public) > public > upnp > unspecified address.

	auto ifAddresses = Network::getInterfaceAddresses();
	auto laddr = m_netPrefs.listenIPAddress.empty() ? bi::address() : bi::address::from_string(m_netPrefs.listenIPAddress);
	auto lset = !laddr.is_unspecified();
	auto paddr = m_netPrefs.publicIPAddress.empty() ? bi::address() : bi::address::from_string(m_netPrefs.publicIPAddress);
	auto pset = !paddr.is_unspecified();

	bool listenIsPublic = lset && isPublicAddress(laddr);
	bool publicIsHost = !lset && pset && ifAddresses.count(paddr);

	bi::tcp::endpoint ep(bi::address(), m_listenPort);
	if (m_netPrefs.traverseNAT && listenIsPublic)
	{
		LOG(INFO) << "Listen address set to Public address:" << laddr << ". UPnP disabled.";
		ep.address(laddr);
	}
	else if (m_netPrefs.traverseNAT && publicIsHost)
	{
		LOG(INFO) << "Public address set to Host configured address:" << paddr << ". UPnP disabled.";
		ep.address(paddr);
	}
	else if (m_netPrefs.traverseNAT)
	{
		bi::address natIFAddr;
		ep = Network::traverseNAT(lset && ifAddresses.count(laddr) ? std::set<bi::address>({laddr}) : ifAddresses, m_listenPort, natIFAddr);

		if (lset && natIFAddr != laddr)
			// if listen address is set, Host will use it, even if upnp returns different
			LOG(WARNING) << "Listen address:" << laddr << " differs from local address:" << natIFAddr << " returned by UPnP!";

		if (pset && ep.address() != paddr)
		{
			// if public address is set, Host will advertise it, even if upnp returns different
			LOG(WARNING) << "Specified public address:" << paddr << " differs from external address:" << ep.address() << " returned by UPnP!";
			ep.address(paddr);
		}
	}
	else if (pset)
		ep.address(paddr);

	m_tcpPublic = ep;
}


void Host::sslHandshakeServer(const boost::system::error_code& error, std::shared_ptr<RLPXSocket> socket)
{
	if (error)
	{
		LOG(WARNING) << "Host::async_handshake err:" << error.message();
	}

	bool success = false;
	try
	{
		// incoming connection; we don't yet know nodeid
		auto handshake = make_shared<RLPXHandshake>(this, socket);
		m_connecting.push_back(handshake);
		handshake->start();//startPeerSession after handshake successfully
		success = true;
	}
	catch (Exception const& _e)
	{
		LOG(WARNING) << "ERROR: " << diagnostic_information(_e);
	}
	catch (std::exception const& _e)
	{
		LOG(WARNING) << "ERROR: " << _e.what();
	}
	if (!success)
		socket->ref().close();
	runAcceptor(); //accept p2p connection
}


bool Host::sslVerifyCert(bool preverified, ba::ssl::verify_context& ctx)
{
	ParseCert parseCert;
	parseCert.ParseInfo(ctx);
	string subjectName = parseCert.getSubjectName();//get subject information of the credential
	int certType = parseCert.getCertType();//get type of the credential(CA: 0, user credential: 1)
	bool isExpire = parseCert.getExpire();//certificate expired or not 
	string serialNumber = parseCert.getSerialNumber();//get serial number of the certificate
	LOG(DEBUG) << "subjectName:" << subjectName;
	LOG(DEBUG) << "serialNumber:" << serialNumber;
	LOG(DEBUG) << "preverified:" << preverified;
	if (isExpire)
	{
		preverified = false;
		LOG(WARNING)<< "Verify Certificate Expire Data Err Please Use The Right Certificate File Retry.....................";
	}

	bool lresult = true;
	if (certType == 1)
	{
		lresult = NodeConnManagerSingleton::GetInstance().CheckConnectCert(serialNumber, m_tcpClient.address().to_string()); //验证证书序列号是否存在CA系统合约中
		if (lresult == false)
		{
			preverified = false;
			LOG(WARNING)<< "System Contract Verify Certificate Err Please Use The Right Certificate File Retry.....................";
		}
	}
	if (preverified == false && lresult == true)
	{
		LOG(WARNING)<< "Verify Certificate Err Please Use The Right Certificate File Retry.....................";
	}
	return preverified;
}

void Host::runAcceptor()
{
	assert(m_listenPort > 0);

	if (m_run && !m_accepting)
	{
		LOG(INFO) << "Listening on local port " << m_listenPort << " (public: " << m_tcpPublic << ")";
		m_accepting = true;

		LOG(INFO) << "begin accepting p2p connections";
		auto socket = make_shared<RLPXSocket>(m_ioService);
		//ssl connection
		if (socket->getSocketType() == SSL_SOCKET_V1)
		{
			socket->sslref().set_verify_mode(ba::ssl::verify_peer);
			socket->sslref().set_verify_callback(boost::bind(&Host::sslVerifyCert, this, _1, _2));
		}

		m_tcp4Acceptor.async_accept(socket->ref(), [ = ](boost::system::error_code ec)
		{
			auto remoteEndpoint = socket->ref().remote_endpoint();
			LOG(INFO) << "Accept New P2P Connection: " << remoteEndpoint.address().to_string() << ":" << remoteEndpoint.port();

			m_accepting = false;
			if (ec || !m_run)
			{
				socket->close();
				return;
			}
			if (peerCount() > peerSlots(Ingress))
			{
				LOG(INFO) << "Dropping incoming connect due to maximum peer count (" << Ingress << " * ideal peer count): " << socket->remoteEndpoint();
				socket->close();
				if (ec.value() < 1)
					runAcceptor();
				return;
			}

			if (socket->getSocketType() == SSL_SOCKET_V1)
			{
				m_tcpClient = socket->remoteEndpoint();//get (ip, port) of the connector, (ip, port) is used to verify white list
				LOG(DEBUG) << "client port:" << socket->remoteEndpoint().port() << "|ip:" << socket->remoteEndpoint().address().to_string();
				LOG(DEBUG) << "server port:" << m_listenPort << "|ip:" << m_tcpPublic.address().to_string();
				socket->sslref().async_handshake(ba::ssl::stream_base::server, boost::bind(&Host::sslHandshakeServer, this, ba::placeholders::error, socket));
			}
			else
			{
				bool success = false;
				try
				{
					// incoming connection; we don't yet know nodeid
					auto handshake = make_shared<RLPXHandshake>(this, socket);
					m_connecting.push_back(handshake);
					handshake->start();//start peer session after handshake
					success = true;
				}
				catch (Exception const& _e)
				{
					LOG(WARNING) << "ERROR: " << diagnostic_information(_e);
				}
				catch (std::exception const& _e)
				{
					LOG(WARNING) << "ERROR: " << _e.what();
				}

				if (!success)
					socket->ref().close();
				runAcceptor(); 
			}
		});
	}
}


void Host::doneWorking()
{
	// reset ioservice (cancels all timers and allows manually polling network, below)
	m_ioService.reset();

	DEV_GUARDED(x_timers)
	m_timers.clear();

	// shutdown acceptor
	if (m_tcp4Acceptor.is_open())
	{
		m_tcp4Acceptor.close();
		m_tcp4Acceptor.cancel();
	}

	// There maybe an incoming connection which started but hasn't finished.
	// Wait for acceptor to end itself instead of assuming it's complete.
	// This helps ensure a peer isn't stopped at the same time it's starting
	// and that socket for pending connection is closed.
	while (m_accepting)
		m_ioService.poll();

	// stop capabilities (eth: stops syncing or block/tx broadcast)
	for (auto const& h : m_capabilities)
		h.second->onStopping();

	// disconnect pending handshake, before peers, as a handshake may create a peer
	for (unsigned n = 0;; n = 0)
	{
		DEV_GUARDED(x_connecting)
		for (auto const& i : m_connecting)
			if (auto h = i.lock())
			{
				h->cancel();
				n++;
			}
		if (!n)
			break;
		m_ioService.poll();
	}

	// disconnect peers
	for (unsigned n = 0;; n = 0)
	{
		DEV_RECURSIVE_GUARDED(x_sessions)
		for (auto i : m_sessions)
			if (auto p = i.second.lock())
				if (p->isConnected())
				{
					p->disconnect(ClientQuit);
					n++;
				}
		if (!n)
			break;

		// poll so that peers send out disconnect packets
		m_ioService.poll();
	}

	// stop network (again; helpful to call before subsequent reset())
	m_ioService.stop();

	// reset network (allows reusing ioservice in future)
	m_ioService.reset();

	// finally, clear out peers (in case they're lingering)
	RecursiveGuard l(x_sessions);
	m_sessions.clear();
}

void Host::addPeer(NodeSpec const& _s, PeerType _t)
{
	LOG(TRACE) << "Host::addPeer" << _s.enode();

	if (_t == PeerType::Optional)
	{
		LOG(TRACE) << "Host::addPeer Optional";
		addNode(_s.id(), _s.nodeIPEndpoint());
	}
	else
	{
		LOG(TRACE) << "Host::addPeer Required";
		requirePeer(_s.id(), _s.nodeIPEndpoint());
	}

}

void Host::addNode(NodeID const& _node, NodeIPEndpoint const& _endpoint)
{
	// return if network is stopped while waiting on Host::run() or nodeTable to start
	while (!haveNetwork())
		if (isWorking())
			this_thread::sleep_for(chrono::milliseconds(50));
		else
			return;

	if (_endpoint.tcpPort < 30300 || _endpoint.tcpPort > 30305)
		LOG(INFO) << "Non-standard port being recorded: " << _endpoint.tcpPort;

	if (m_nodeTable)
		m_nodeTable->addNode(Node(_node, _endpoint));
}

void Host::requirePeer(NodeID const& _n, NodeIPEndpoint const& _endpoint)
{
	LOG(TRACE) << "Host::requirePeer" << _n.abridged();

	m_requiredPeers.insert(_n);

	if (!m_run)
		return;

	Node node(_n, _endpoint, PeerType::Required);
	if (_n)
	{
		// create or update m_peers entry
		shared_ptr<Peer> p;
		DEV_RECURSIVE_GUARDED(x_sessions)
		if (m_peers.count(_n))
		{
			p = m_peers[_n];
			p->endpoint = node.endpoint;
			p->peerType = PeerType::Required;
		}
		else
		{
			p = make_shared<Peer>(node);
			m_peers[_n] = p;
		}
		// required for discovery
		if (m_nodeTable)
			m_nodeTable->addNode(*p, NodeTable::NodeRelation::Known);
	}
	else if (m_nodeTable)
	{
		m_nodeTable->addNode(node);
		auto t = make_shared<boost::asio::deadline_timer>(m_ioService);
		t->expires_from_now(boost::posix_time::milliseconds(600));
		t->async_wait([this, _n](boost::system::error_code const & _ec)
		{
			if (!_ec)
				if (m_nodeTable)
					if (auto n = m_nodeTable->node(_n))
						requirePeer(n.id, n.endpoint);
		});
		DEV_GUARDED(x_timers)
		m_timers.push_back(t);
	}
}

void Host::relinquishPeer(NodeID const& _node)
{
	Guard l(x_requiredPeers);
	if (m_requiredPeers.count(_node))
		m_requiredPeers.erase(_node);
}

void Host::sslHandshakeClient(const boost::system::error_code& error, std::shared_ptr<RLPXSocket> socket, NodeID id, Peer* nptr)
{
	if (error)
	{
		LOG(DEBUG) << "Host::sslHandshakeClient err:" << error.message();
	}

	auto handshake = make_shared<RLPXHandshake>(this, socket, id);
	{
		Guard l(x_connecting);
		m_connecting.push_back(handshake);
	}
	handshake->start();

	Guard l(x_pendingNodeConns);
	m_pendingPeerConns.erase(nptr);
}

void Host::connect(std::shared_ptr<Peer> const& _p)
{
	if (!m_run)
		return;

	if (havePeerSession(_p->id))
	{
		LOG(INFO) << "Aborted connect. Node already connected.";
		return;
	}

	if (!!m_nodeTable && !m_nodeTable->haveNode(_p->id) && _p->peerType == PeerType::Optional)
		return;

	// prevent concurrently connecting to a node
	Peer *nptr = _p.get();
	{
		Guard l(x_pendingNodeConns);
		if (m_pendingPeerConns.count(nptr))
			return;
		m_pendingPeerConns.insert(nptr);
	}

	_p->m_lastAttempted = std::chrono::system_clock::now();

	bi::tcp::endpoint ep(_p->endpoint);
	LOG(INFO) << "Attempting connection to node " << _p->id.abridged() << "@" << ep << " from" << id();
	auto socket = make_shared<RLPXSocket>(m_ioService);

	// Verify certificate of the connected peer
	if (socket->getSocketType() == SSL_SOCKET_V1)
	{
		m_tcpClient = socket->remoteEndpoint();//get (IP, Port) of the connected peer to verify the white list
		socket->sslref().set_verify_mode(ba::ssl::verify_peer);
		socket->sslref().set_verify_callback(boost::bind(&Host::sslVerifyCert, this, _1, _2));
	}
	socket->ref().async_connect(ep, [ = ](boost::system::error_code const & ec)
	{
		_p->m_lastAttempted = std::chrono::system_clock::now();
		_p->m_failedAttempts++;

		if (ec)
		{
			LOG(WARNING) << "Connection refused to node" << _p->id.abridged() << "@" << ep << "(" << ec.message() << ")";
			// Manually set error (session not present)
			_p->m_lastDisconnect = TCPError;

			Guard l(x_pendingNodeConns);
			m_pendingPeerConns.erase(nptr);
		}
		else
		{
			if (socket->getSocketType() == SSL_SOCKET_V1)
			{
				socket->sslref().async_handshake(ba::ssl::stream_base::client, boost::bind(&Host::sslHandshakeClient, this, ba::placeholders::error, socket, _p->id, nptr));
			}
			else
			{
				LOG(INFO) << "Connecting to" << _p->id.abridged() << "@" << ep;
				auto handshake = make_shared<RLPXHandshake>(this, socket, _p->id);
				{
					Guard l(x_connecting);
					m_connecting.push_back(handshake);
				}
				handshake->start();

				Guard l(x_pendingNodeConns);
				m_pendingPeerConns.erase(nptr);
			}
		}
	});
}


void Host::run(boost::system::error_code const&)
{
	if (!m_run)
	{
		// reset NodeTable
		m_nodeTable.reset();

		// stopping io service allows running manual network operations for shutdown
		// and also stops blocking worker thread, allowing worker thread to exit
		m_ioService.stop();

		// resetting timer signals network that nothing else can be scheduled to run
		m_timer.reset();
		return;
	}

	m_nodeTable->processEvents();// 定时循环 回来处理事件

	// cleanup zombies
	DEV_GUARDED(x_connecting)
	m_connecting.remove_if([](std::weak_ptr<RLPXHandshake> h) { return h.expired(); });
	DEV_GUARDED(x_timers)
	m_timers.remove_if([](std::shared_ptr<boost::asio::deadline_timer> t)
	{
		return t->expires_from_now().total_milliseconds() < 0;
	});

	keepAlivePeers();

	// At this time peers will be disconnected based on natural TCP timeout.
	// disconnectLatePeers needs to be updated for the assumption that Session
	// is always live and to ensure reputation and fallback timers are properly
	// updated. // disconnectLatePeers();

	// todo: update peerSlotsAvailable()

	/*
	list<shared_ptr<Peer>> toConnect;
	unsigned reqConn = 0;
	{
		RecursiveGuard l(x_sessions);
		for (auto const& p : m_peers)
		{
			bool haveSession = havePeerSession(p.second->id);
			bool required = p.second->peerType == PeerType::Required;
			if (haveSession && required)
				reqConn++;
			else if (!haveSession && p.second->shouldReconnect() && (!m_netPrefs.pin || required))
				toConnect.push_back(p.second);
		}
	}

	for (auto p : toConnect)
		if (p->peerType == PeerType::Required && reqConn++ < m_idealPeerCount)
			connect(p);


	if (!m_netPrefs.pin)
	{
		unsigned pendingCount = 0;
		DEV_GUARDED(x_pendingNodeConns)
		pendingCount = m_pendingPeerConns.size();
		int openSlots = m_idealPeerCount - peerCount() - pendingCount + reqConn;
		if (openSlots > 0)
			for (auto p : toConnect)
				if (p->peerType == PeerType::Optional && openSlots--)
					connect(p);
	}
	*/

	//reconnect disconnected peers
	reconnectAllNodes();

	auto runcb = [this](boost::system::error_code const & error) { run(error); };
	m_timer->expires_from_now(boost::posix_time::milliseconds(c_timerInterval));
	m_timer->async_wait(runcb);   //callback run()
}

// callback startedWorking firstly before callback dowork function of work class
void Host::startedWorking()
{
	asserts(!m_timer);

	{
		// prevent m_run from being set to true at same time as set to false by stop()
		// don't release mutex until m_timer is set so in case stop() is called at same
		// time, stop will wait on m_timer and graceful network shutdown.
		Guard l(x_runTimer);
		// create deadline timer
		m_timer.reset(new boost::asio::deadline_timer(m_ioService));
		m_run = true;
	}

	// start capability threads (ready for incoming connections)
	for (auto const& h : m_capabilities)
		h.second->onStarting();

	// try to open acceptor (todo: ipv6)
	// accept connections of peers
	int port = Network::tcp4Listen(m_tcp4Acceptor, m_netPrefs);
	if (port > 0)
	{
		m_listenPort = port;
		determinePublic();
		runAcceptor(); 
	}
	else
	{
		LOG(ERROR) << "p2p.start.notice id:" << id() << "TCP Listen port is invalid or unavailable.";
		LOG(ERROR) << "P2pPort bind failed！" << "\n";
		exit(-1);
	}


	auto nodeTable = make_shared<NodeTable>(
	                     m_ioService,
	                     m_alias,
	                     NodeIPEndpoint(bi::address::from_string(listenAddress()), listenPort(), listenPort()),
	                     m_netPrefs.discovery
	                 );
	nodeTable->setEventHandler(new HostNodeTableHandler(*this));
	//load endpoint information of peers into node table from configuration file(config.json)
	m_nodeTable = nodeTable;
	restoreNetwork(&m_restoreNetwork);

	LOG(INFO) << "p2p.started id:" << id();

	run(boost::system::error_code());
}



void Host::keepAlivePeers()
{
	if (chrono::steady_clock::now() - c_keepAliveInterval < m_lastPing)
		return;

	RecursiveGuard l(x_sessions);
	for (auto it = m_sessions.begin(); it != m_sessions.end();) {
		if (auto p = it->second.lock())
		{
			p->ping();
			++it;
		}
		else {
			LOG(WARNING) << "Host::keepAlivePeers erase " << it->first;
			it = m_sessions.erase(it);
		}
	}

	m_lastPing = chrono::steady_clock::now();
}

void Host::reconnectAllNodes()
{
	if (chrono::steady_clock::now() - c_reconnectNodesInterval < m_lastReconnect)
		return;

	// load endpoint information of peers from both system contract and configuration file (config.json)
	std::map<std::string, eth::NodeConnParams> mNodeConnParams;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfoContractAndConf(mNodeConnParams);

	RecursiveGuard l(x_sessions);
	// reconnect disconnected peers 
	for (auto stNode : mNodeConnParams)
	{
		if (m_sessions.find(dev::jsToPublic(dev::toJS(stNode.first))) == m_sessions.end() && stNode.first != id().hex())
		{
			LOG(INFO) << " reconnect node :" << stNode.first << ". myself is " << id().abridged() << "\n";
			NodeConnManagerSingleton::GetInstance().connNode(stNode.second);
		}
	}

	m_lastReconnect = chrono::steady_clock::now();
}

void Host::disconnectLatePeers()
{
	auto now = chrono::steady_clock::now();
	if (now - c_keepAliveTimeOut < m_lastPing)
		return;

	RecursiveGuard l(x_sessions);
	for (auto p : m_sessions)
		if (auto pp = p.second.lock())
			if (now - c_keepAliveTimeOut > m_lastPing && pp->lastReceived() < m_lastPing)
				pp->disconnect(PingTimeout);
}

void Host::disconnectByNodeId(const std::string &sNodeId)
{
	if (id().hex() == sNodeId)
	{
		LOG(WARNING) << "disconnectByNodeId  self " << id().abridged() << "|" << sNodeId << "\n";

		return;
	}

	RecursiveGuard l(x_sessions);
	if (m_sessions.find(dev::jsToPublic(dev::toJS(sNodeId))) != m_sessions.end())
	{
		auto pp = m_sessions[jsToPublic(toJS(sNodeId))].lock();
		if (pp && pp->isConnected())
		{
			if (pp->isConnected())
			{
				pp->disconnect(UserReason);
			}
		}
	}
	else {
		LOG(WARNING) << "disconnectByNodeId  can not find " << sNodeId << "\n";
	}

}

void Host::disconnectByPub256(const std::string &_pub256)
{
	RecursiveGuard l(x_sessions);
	std::unordered_map<NodeID, std::weak_ptr<SessionFace>>::iterator it = m_sessions.begin();
	while (it != m_sessions.end())
	{
		auto sp = it->second.lock();
		if (sp)
		{
			auto baseDataPtr = sp->getCABaseData();
			if (!baseDataPtr)
			{
				LOG(WARNING) << "disconnectByPub256 baseData is nullptr" << "\n";
				return;
			}
			std::string pub256 = baseDataPtr->getPub256();
			if (pub256 == _pub256)
			{
				LOG(WARNING) << "disconnectByPub256 nodeId:" << sp->id().abridged() << ",pub256" << _pub256 << "\n";
				disconnectByNodeId(sp->id().hex());
				return;
			}
		}
		it++;
	}

	LOG(WARNING) << "not found pub256:" << _pub256 << "\n";
}

void Host::recheckCAByPub256(const std::string &_pub256)
{
	LOG(INFO) << "Host recheckCAByPub256 pub256:" << _pub256 << "\n";
	RecursiveGuard l(x_sessions);
	std::unordered_map<NodeID, std::weak_ptr<SessionFace>>::iterator it = m_sessions.begin();
	while (it != m_sessions.end())
	{
		auto sp = it->second.lock();
		if (sp && sp->isConnected())
		{
			auto baseDataPtr = sp->getCABaseData();
			if (!baseDataPtr)
			{
				LOG(ERROR) << "recheckCAByPub256 baseData is nullptr" << "\n";
				return;
			}
			std::string pub256 = baseDataPtr->getPub256();
			if (pub256 == _pub256)
			{
				WBCAData wbCAData;
				wbCAData.setSeed(baseDataPtr->getSeed());
				wbCAData.setSign(baseDataPtr->getSign());
				wbCAData.setNodeSign(baseDataPtr->getNodeSign());
				wbCAData.setPub256(baseDataPtr->getPub256());
				bool ok = NodeConnManagerSingleton::GetInstance().CheckAll(sp->id().hex(), wbCAData);
				if (!ok)
				{
					LOG(WARNING) << "recheckCAByPub256 false.nodeId:" << sp->id().abridged() << ",pub256" << _pub256 << "\n";
					disconnectByNodeId(sp->id().hex());
				}
				return;
			}
		}

		it++;
	}
}

void Host::recheckAllCA()
{
	LOG(INFO) << "Host recheckAllCA" << "\n";
	RecursiveGuard l(x_sessions);
	std::unordered_map<NodeID, std::weak_ptr<SessionFace>>::iterator it = m_sessions.begin();
	while (it != m_sessions.end())
	{
		auto sp = it->second.lock();
		if (sp && sp->isConnected())
		{
			auto baseDataPtr = sp->getCABaseData();
			WBCAData wbCAData;
			wbCAData.setSeed(baseDataPtr->getSeed());
			wbCAData.setSign(baseDataPtr->getSign());
			wbCAData.setNodeSign(baseDataPtr->getNodeSign());
			wbCAData.setPub256(baseDataPtr->getPub256());
			bool ok = NodeConnManagerSingleton::GetInstance().CheckAll(sp->id().hex(), wbCAData);
			if (!ok)
			{
				LOG(WARNING) << "recheckCAByPub256 nodeId:" << sp->id().abridged() << "\n";
				disconnectByNodeId(sp->id().hex());
			}
		}
		it++;
	}
}

// save ca data into session
void Host::saveCADataByNodeId(const std::string nodeId, CABaseData &baseData)
{
	RecursiveGuard l(x_sessions);
	//set CA data into session related to nodeId
	if (m_sessions.find(dev::jsToPublic(dev::toJS(nodeId))) != m_sessions.end())
	{
		auto pp = m_sessions[jsToPublic(toJS(nodeId))].lock();
		if (pp->isConnected())
		{
			LOG(INFO) << "Host saveCADataByNodeId, nodeId:" << nodeId << ", seed:" << baseData.getSeed() << ",pub256:" << baseData.getPub256() << ",nodeSign:" << baseData.getNodeSign() << "\n";
			WBCAData *ptr = new WBCAData;
			ptr->setSeed(baseData.getSeed());
			ptr->setSign(baseData.getSign());
			ptr->setPub256(baseData.getPub256());
			ptr->setNodeSign(baseData.getNodeSign());
			pp->saveCABaseData(ptr);
		}
		else
		{
			LOG(WARNING) << "saveCADataByNodeId nodeId:" << nodeId << " is not connected." << "\n";
		}

	}
	else {
		LOG(WARNING) << "saveCADataByNodeId  can not find nodeId:" << nodeId << "\n";
	}
}



void Host::restoreNetwork(bytesConstRef _b)
{
	if (!_b.size())
		return;

	// nodes can only be added if network is added
	if (!isStarted())
		BOOST_THROW_EXCEPTION(NetworkStartRequired());

	if (m_dropPeers)
		return;

	RecursiveGuard l(x_sessions);

	addConnParamsToNodeTable();
}

void Host::addConnParamsToNodeTable()
{
	std::map<std::string, eth::NodeConnParams> mNodeConnParams;
	// load endpoint information into node table from both system contract and configuration file
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfoContractAndConf(mNodeConnParams);
	for (auto param : mNodeConnParams)
	{
		//rule out itself
		if (id().hex() == param.first)
		{
			continue;
		}
		p2p::NodeSpec ns = p2p::NodeSpec(param.second.toEnodeInfo());

		LOG(INFO) << "addConnParamsToNodeTable id: " << ns.enode() << "\n";
		m_nodeTable->addNode(Node(ns.id(), ns.nodeIPEndpoint()), NodeTable::NodeRelation::Known);
	}
}

HostApi* HostSingleton::GetHost(
				std::string const& _clientVersion,
				NetworkPreferences const& _n ,
				bytesConstRef _restoreNetwork ,
				int const& _statisticsInterval 
			)
{
	if (dev::getSSL() == SSL_SOCKET_V2)
	{
		static HostSSL hostSSL(_clientVersion,CertificateServer::GetInstance().keypair(),_n);
		return &hostSSL;
	}
	else
	{
		static Host host(_clientVersion,_n,_restoreNetwork,_statisticsInterval);
		return &host;
	}
   
}

