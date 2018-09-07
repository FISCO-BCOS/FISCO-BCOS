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
#include <libdevcrypto/Rsa.h>
#include "Session.h"
#include "Common.h"
#include "Capability.h"
#include "RLPxHandshake.h"
#include "Host.h"
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

Host::Host(string const& _clientVersion, KeyPair const& _alias, NetworkPreferences const& _n):
	Worker("p2p", 0),
	m_clientVersion(_clientVersion),
	m_netPrefs(_n),
	m_ifAddresses(Network::getInterfaceAddresses()),
	m_ioService(2),
	m_tcp4Acceptor(m_ioService),
	m_alias(_alias),
	m_lastPing(chrono::steady_clock::time_point::min()),
	m_lastReconnect(chrono::steady_clock::time_point::min())
{
	LOG(INFO) << "Id:" << id();
}

Host::Host(string const& _clientVersion, NetworkPreferences const& _n, bytesConstRef _restoreNetwork):
	Host(_clientVersion, networkAlias(_restoreNetwork), _n)
{
	m_restoreNetwork = _restoreNetwork.toBytes();
}

Host::~Host()
{
	stop();
}

void Host::start()
{
	DEV_TIMED_FUNCTION_ABOVE(500);
	startWorking();//启动work循环
	while (isWorking() && !haveNetwork())
		this_thread::sleep_for(chrono::milliseconds(10));

	// network start failed!
	if (isWorking())
		return;

	LOG(WARNING) << "Network start failed!";
	doneWorking();
}

void Host::stop()
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
}

void Host::doneWorking()
{
	// reset ioservice (cancels all timers and allows manually polling network, below)
	m_ioService.reset();

	DEV_GUARDED(x_timers)
	m_timers.clear();

	// shutdown acceptor
	m_tcp4Acceptor.cancel();
	if (m_tcp4Acceptor.is_open())
		m_tcp4Acceptor.close();

	// There maybe an incoming connection which started but hasn't finished.
	// Wait for acceptor to end itself instead of assuming it's complete.
	// This helps ensure a peer isn't stopped at the same time it's starting
	// and that socket for pending connection is closed.
	while (m_accepting)
		m_ioService.poll();

	// stop capabilities (eth: stops syncing or block/tx broadcast)
	for (auto const& h : m_capabilities)
		if(h.second) {
			h.second->onStopping();
		}

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

void Host::startPeerSession(Public const& _id, RLP const& _rlp, std::shared_ptr<RLPXSocket> const& _s)
{
	auto protocolVersion = _rlp[0].toInt<unsigned>();
	LOG(DEBUG) << "protocolVersion: " << protocolVersion;

	auto clientVersion = _rlp[1].toString();
	LOG(DEBUG) << "clientVersion: " << clientVersion;

	auto caps = _rlp[2].toVector<CapDesc>();//通信信道

	auto listenPort = _rlp[3].toInt<unsigned short>();
	//auto pub = _rlp[4].toHash<Public>(); //_rlp[4]是节点pub, 暂时不用

	// session maybe ingress or egress so m_peers and node table entries may not exist

	Public id = _id;
	if(id == NodeID()) {
		LOG(ERROR) << "No nodeid! disconnect";
		_s->close();

		return;
	}

	shared_ptr<Peer> p;
	DEV_RECURSIVE_GUARDED(x_sessions)
	{
		if (m_peers.count(id))
			p = m_peers[id];
		else
		{
			if (!p)
				p = make_shared<Peer>(Node(id, UnspecifiedNodeIPEndpoint));

			m_peers[id] = p;
		}
	}
	if (p->isOffline())
		p->m_lastConnected = std::chrono::system_clock::now();
	p->endpoint.address = _s->remoteEndpoint().address();

	// clang error (previously: ... << hex << caps ...)
	// "'operator<<' should be declared prior to the call site or in an associated namespace of one of its arguments"
	stringstream capslog;

	// leave only highset mutually supported capability version
	caps.erase(remove_if(caps.begin(), caps.end(), [&](CapDesc const & _r) { return !haveCapability(_r) || any_of(caps.begin(), caps.end(), [&](CapDesc const & _o) { return _r.first == _o.first && _o.second > _r.second && haveCapability(_o); }); }), caps.end());

	for (auto cap : caps)
		capslog << "(" << cap.first << "," << dec << cap.second << ")";

	LOG(INFO) << "Hello: " << clientVersion << "V[" << protocolVersion << "]" << id << showbase << capslog.str() << dec << listenPort;

	// create session so disconnects are managed
	shared_ptr<SessionFace> ps = make_shared<Session>(this, _s, p, PeerSessionInfo({id, clientVersion, p->endpoint.address.to_string(), listenPort, chrono::steady_clock::duration(), _rlp[2].toSet<CapDesc>(), 0, map<string, string>(), protocolVersion}));

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

	if (m_netPrefs.pin && !m_requiredPeers.count(id))
	{
		LOG(DEBUG) << "Unexpected identity from peer (got" << id << ", must be one of " << m_requiredPeers << ")";
		ps->disconnect(UnexpectedIdentity);
		return;
	}

	{
		RecursiveGuard l(x_sessions);
		if (m_sessions.count(id) && !!m_sessions[id].lock()) {
			if (auto s = m_sessions[id].lock()) {
				if (s->isConnected())
				{
					// Already connected.
					LOG(WARNING) << "Session already exists for peer with id: " << id;
					ps->disconnect(DuplicatePeer);
					return;
				}
			}

			NodeIPEndpoint endpoint(_s->remoteEndpoint().address(), 0, _s->remoteEndpoint().port());
			auto it = _staticNodes.find(endpoint);
			if(it != _staticNodes.end()) {
				it->second = id;
			}
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
				//里面会调用ps的newPeerCapability
				//也会调用EthereumPeer的init lim 里面会发个StatusPacket包
				offset += pcap->messageCount();
			}
		}

		ps->start();//启动session 发个ping包
		m_sessions[id] = ps;
	}

	LOG(INFO) << "p2p.host.peer.register: " << id;
}

bool Host::isConnected(const NodeID &nodeID) const {
	auto session = peerSession(nodeID);
	if(session && session->isConnected()) {
		return true;
	}

	return false;
}

void Host::determinePublic()
{
}


void Host::runAcceptor()
{
	assert(m_listenPort > 0);

	if (m_run && !m_accepting)
	{
		LOG(INFO) << "Listening on local port " << m_listenPort << " (public: " << m_tcpPublic << ")";
		m_accepting = true;

		auto socket = make_shared<RLPXSocket>(m_ioService, *_sslContext);

		m_tcp4Acceptor.async_accept(socket->ref().lowest_layer(), [ = ](boost::system::error_code ec)
		{
			auto remoteEndpoint = socket->remoteEndpoint();
			LOG(INFO) << "P2P receive new connection: " << remoteEndpoint.address().to_string() << ":" << remoteEndpoint.port();

			m_accepting = false;
			if (ec || !m_run)
			{
				socket->close();
				return;
			}

			if (peerCount() > peerSlots(Ingress))
			{
				LOG(INFO) << "达到最大连接数，断开 (" << Ingress << " * ideal peer count): " << socket->remoteEndpoint();
				socket->close();
				if (ec.value() < 1)
					runAcceptor();
				return;
			}

			std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
			socket->ref().set_verify_callback(newVerifyCallback(endpointPublicKey));

			//进行ssl握手
			socket->ref().async_handshake(boost::asio::ssl::stream_base::server, [ = ] (const boost::system::error_code& error) {
				if(error || !m_run) {
					LOG(ERROR) << "P2P Handshake failed: " << error.message();

					socket->close();
					return;
				}

				dev::p2p::NodeID endpointNodeID(*endpointPublicKey);
				if(endpointNodeID == id()) {
					//连接到自己了
					LOG(TRACE) << "Disconnect self" << endpointNodeID;

					socket->close();

					return;
				}

				bool success = false;

				try
				{
					auto handshake = std::make_shared<RLPXHandshake>(this, socket, dev::p2p::NodeID(*endpointPublicKey));
					m_connecting.push_back(handshake);
					handshake->start();//握手成功后startPeerSession
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

				if (!success) socket->close();
			});

			runAcceptor();
		});
	}
}

std::unordered_map<Public, std::string> Host::pocHosts()
{
	return { };
}

void Host::addPeer(NodeSpec const& _s, PeerType _t)
{
}

void Host::relinquishPeer(NodeID const& _node)
{
}

void Host::connect(NodeIPEndpoint const &endpoint)
{
	if (!m_run)
		return;

	//防止重复连接
	{
		Guard l(x_pendingNodeConns);
		if (m_pendingPeerConns.count(endpoint))
			return;
		m_pendingPeerConns.insert(endpoint);
	}

	LOG(INFO) << "Attempting connection to node " << endpoint.address.to_string() << ":" << endpoint.tcpPort;;
	auto socket = make_shared<RLPXSocket>(m_ioService, *_sslContext);

	//客户端验证证书有效性
	socket->ref().lowest_layer().async_connect(endpoint, [ = ](boost::system::error_code const & ec)
	{
		if (ec)
		{
			LOG(ERROR) << "Connection refused to node " << endpoint << "(" << ec.message() << ")";
			// Manually set error (session not present)

			Guard l(x_pendingNodeConns);
			m_pendingPeerConns.erase(endpoint);
		}
		else
		{
			LOG(INFO) << "Connecting to " << endpoint.address.to_string() << ":" << endpoint.tcpPort;

			std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
			socket->ref().set_verify_callback(newVerifyCallback(endpointPublicKey));

			//增加一步ssl handshake
			socket->ref().async_handshake(boost::asio::ssl::stream_base::client, [ = ](boost::system::error_code const & ec) {
				if(ec) {
					LOG(ERROR) << "P2P Handshake refused to node "
							<< endpoint.address.to_string() << ":" << endpoint.tcpPort
							<< "(" << ec.message() << ")";

					Guard l(x_pendingNodeConns);
					m_pendingPeerConns.erase(endpoint);

					socket->close();

					return;
				}

				LOG(INFO) << "Handshake to " << *endpointPublicKey << "@" << endpoint.address.to_string() << ":" << endpoint.tcpPort;
				dev::p2p::NodeID endpointNodeID(*endpointPublicKey);
				if(endpointNodeID == id()) {
					//连接到自己了
					LOG(TRACE) << "Disconnect self" << endpointNodeID;

					Guard l(x_pendingNodeConns);
					m_pendingPeerConns.erase(endpoint);

					RecursiveGuard m(x_sessions);
					auto it = _staticNodes.find(endpoint);
					if(it != _staticNodes.end()) {
						it->second = endpointNodeID;
					}

					socket->close();

					return;
				}

				auto handshake = make_shared<RLPXHandshake>(this, socket, endpointNodeID);
				{
					Guard l(x_connecting);
					m_connecting.push_back(handshake);
				}
				handshake->start();

				Guard l(x_pendingNodeConns);
				m_pendingPeerConns.erase(endpoint);
			});

		}
	});
}

PeerSessionInfos Host::peerSessionInfo() const
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


size_t Host::peerCount() const
{
	unsigned retCount = 0;
	RecursiveGuard l(x_sessions);
	for (auto& i : m_sessions)
		if (std::shared_ptr<SessionFace> j = i.second.lock())
			if (j->isConnected())
				retCount++;
	return retCount;
}

void Host::run(boost::system::error_code const&)
{
	if (!m_run)
	{
		// reset NodeTable
		//m_nodeTable.reset();

		LOG(ERROR) << "Host loop stoped";

		// stopping io service allows running manual network operations for shutdown
		// and also stops blocking worker thread, allowing worker thread to exit
		m_ioService.stop();

		// resetting timer signals network that nothing else can be scheduled to run
		m_timer.reset();
		return;
	}

	//m_nodeTable->processEvents();// 定时循环 回来处理事件

	// cleanup zombies
	DEV_GUARDED(x_connecting)
	m_connecting.remove_if([](std::weak_ptr<RLPXHandshake> h) { return h.expired(); });
	DEV_GUARDED(x_timers)
	m_timers.remove_if([](std::shared_ptr<boost::asio::deadline_timer> t)
	{
		return t->expires_from_now().total_milliseconds() < 0;
	});

	keepAlivePeers();

	//查看是否所有节点已经连接 如果断掉则进行连接
	reconnectAllNodes();

	auto runcb = [this](boost::system::error_code const & error) { run(error); };
	m_timer->expires_from_now(boost::posix_time::milliseconds(c_timerInterval));
	m_timer->async_wait(runcb);   //不断回调run()
}

//work dowork循环之前，先执行这个
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
	//在调这里之前Client初始化的时候已经将EthereumHost注册进来registerCapability
	// start capability threads (ready for incoming connections)
	//进行连接 根据已连接的状态值进行连接
	for (auto const& h : m_capabilities)
		h.second->onStarting();// 启动多个线程

	// try to open acceptor (todo: ipv6)
	//已经进行bind和listen
	int port = Network::tcp4Listen(m_tcp4Acceptor, m_netPrefs);
	if (port > 0)
	{
		m_listenPort = port;
		determinePublic();
		runAcceptor();
	}
	else
	{
		LOG(INFO) << "p2p.start.notice id:" << id() << "TCP Listen port is invalid or unavailable.";
		LOG(ERROR) << "P2pPort bind failed！" << "\n";
		exit(-1);
	}

	LOG(INFO) << "p2p.started id:" << id();

	run(boost::system::error_code());//第一次执行
}

void Host::doWork()
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

	RecursiveGuard l(x_sessions);

	LOG(TRACE) << "Try reconnect all node: " << _staticNodes.size();

	//尝试连接静态配置列表中的所有节点
	for (auto it : _staticNodes) {
		LOG(DEBUG) << "try connect: " << it.first.address.to_string() << ":" << it.first.tcpPort;

		//忽略自己
		if((
				(!m_netPrefs.listenIPAddress.empty() && it.first.address == boost::asio::ip::address::from_string(m_netPrefs.listenIPAddress)) ||
				(!m_netPrefs.publicIPAddress.empty() && it.first.address == boost::asio::ip::address::from_string(m_netPrefs.publicIPAddress)) ||
				m_ifAddresses.find(it.first.address) != m_ifAddresses.end() ||
				it.first.address == m_tcpPublic.address() ||
				it.first.address == m_tcpClient.address())
				&& it.first.tcpPort == m_netPrefs.listenPort) {
			LOG(TRACE) << "Ignore connect self" << it.first;

			continue;
		}

		if(it.second == id()) {
			LOG(TRACE) << "Ignore connect self: " << it.first;

			continue;
		}

		if (it.second != NodeID() && isConnected(it.second)) {
			LOG(TRACE) << "Ignore connected node: " << it.second;
			//已连接 忽略
			continue;
		}

		connect(it.first);
	}

	m_lastReconnect = chrono::steady_clock::now();
}

void Host::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext) {
	_sslContext = sslContext;
}

std::map<NodeIPEndpoint, NodeID>* Host::staticNodes() {
	return &_staticNodes;
}

void Host::setStaticNodes(const std::map<NodeIPEndpoint, NodeID> &staticNodes) {
	_staticNodes = staticNodes;
}

bytes Host::saveNetwork() const
{
	RLPStream ret(3);
	ret << dev::p2p::c_protocolVersion << m_alias.secret().ref();
	int count = 0;
	ret.appendList(count);
	/*if (!!count)
		ret.appendRaw(network.out(), count);*/
	return ret.out();
}

KeyPair Host::networkAlias(bytesConstRef _b)
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

std::function<bool(bool, boost::asio::ssl::verify_context&)> Host::newVerifyCallback(std::shared_ptr<std::string> nodeIDOut) {
	return [nodeIDOut] (bool preverified, boost::asio::ssl::verify_context& ctx) {
		try {
			X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
			if(!cert) {
				LOG(ERROR) << "Get cert failed";
				return preverified;
			}

			int crit = 0;
			BASIC_CONSTRAINTS* basic = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
			if(!basic) {
				LOG(ERROR) << "Get ca casic failed";
				return preverified;
			}

			if(basic->ca) {
				//一级或二级CA的证书，忽略
				LOG(TRACE) << "Ignore CA certificate";
				return preverified;
			}

			EVP_PKEY* evpPublicKey = X509_get_pubkey(cert);
			if(!evpPublicKey) {
				LOG(ERROR) << "Get evpPublicKey failed";
				return preverified;
			}

			ec_key_st* ecPublicKey = EVP_PKEY_get1_EC_KEY(evpPublicKey);
			if(!ecPublicKey) {
				LOG(ERROR) << "Get ecPublicKey failed";
				return preverified;
			}

			const EC_POINT* ecPoint = EC_KEY_get0_public_key(ecPublicKey);
			if(!ecPoint) {
				LOG(ERROR) << "Get ecPoint failed";
				return preverified;
			}

			std::shared_ptr<char> hex = std::shared_ptr<char>(
					EC_POINT_point2hex(
							EC_KEY_get0_group(ecPublicKey),
							ecPoint,
							EC_KEY_get_conv_form(ecPublicKey),
							NULL),
					[] (char* p) {
						OPENSSL_free(p);
					}
			);

			if(hex) {
				nodeIDOut->assign(hex.get());
				if(nodeIDOut->find("04") == 0) {
					//移除开头的04
					nodeIDOut->erase(0, 2);
				}
				LOG(DEBUG) << "Get endpoint publickey:" << *nodeIDOut;
			}

			return preverified;
		}
		catch(std::exception &e) {
			LOG(ERROR) << "Cert verify failed: " << boost::diagnostic_information(e);
			return preverified;
		}
	};
}
