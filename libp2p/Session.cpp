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
/** @file Session.cpp
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 */

#include "Session.h"

#include <chrono>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>
#include "Host.h"
#include "Capability.h"
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace dev::p2p;

Session::Session(HostApi* _server, std::unique_ptr<RLPXFrameCoder>&& _io, std::shared_ptr<RLPXSocketApi> const& _s, std::shared_ptr<Peer> const& _n, PeerSessionInfo _info):
	m_server(_server),
	m_io(move(_io)),
	m_socket(_s),
	m_peer(_n),
	m_info(_info),
	m_ping(chrono::steady_clock::time_point::max())
{
	registerFraming(0);
	m_peer->m_lastDisconnect = NoDisconnect;
	m_lastReceived = m_connect = chrono::steady_clock::now();
	DEV_GUARDED(x_info)
	m_info.socketId = m_socket->ref().native_handle();

	
	m_strand=_server->getStrand();
}

Session::~Session()
{
	ThreadContext tc(info().id.abridged());
	ThreadContext tc2(info().clientVersion);
	LOG(INFO) << "Closing peer session, Session will be free";
	m_peer->m_lastConnected = m_peer->m_lastAttempted - chrono::seconds(1);

	// Read-chain finished for one reason or another.
	for (auto& i : m_capabilities)
		i.second.reset();

	try
	{
		bi::tcp::socket& socket = m_socket->ref();
		if (m_socket->isConnected())
		{
			boost::system::error_code ec;

			LOG(WARNING) << "Session::~Session Closing " << socket.remote_endpoint(ec) << ", " << m_peer->address();

			auto sslSocket = m_socket;
			sslSocket->sslref().async_shutdown([sslSocket](const boost::system::error_code& error) {
				if(error) {
					LOG(WARNING) << "Error while shutdown the ssl socket: " << error.message();
				}

				sslSocket->ref().close();
			});
		}
	}
	catch (...) {}

	if (m_CABaseData)
	{
		delete m_CABaseData;
		m_CABaseData = nullptr;
	}
}

ReputationManager& Session::repMan()
{
	return m_server->repMan();
}

NodeID Session::id() const
{
	return m_peer ? m_peer->id : NodeID();
}

void Session::addRating(int _r)
{
	if (m_peer)
	{
		m_peer->m_rating += _r;
		m_peer->m_score += _r;
		if (_r >= 0)
			m_peer->noteSessionGood();
	}
}

int Session::rating() const
{
	return m_peer->m_rating;
}

template <class T> vector<T> randomSelection(vector<T> const& _t, unsigned _n)
{
	if (_t.size() <= _n)
		return _t;
	vector<T> ret = _t;
	while (ret.size() > _n)
	{
		auto i = ret.begin();
		advance(i, rand() % ret.size());
		ret.erase(i);
	}
	return ret;
}

// read package after receive the packae
bool Session::readPacket(uint16_t _capId, PacketType _t, RLP const& _r)
{
	m_lastReceived = chrono::steady_clock::now();
	try // Generic try-catch block designed to capture RLP format errors - TODO: give decent diagnostics, make a bit more specific over what is caught.
	{
		// v4 frame headers are useless, offset packet type used
		// v5 protocol type is in header, packet type not offset
		if (_capId == 0 && _t < UserPacket)
			return interpret(_t, _r);

		if (isFramingEnabled())
		{
			for (auto const& i : m_capabilities)
				if (i.second->c_protocolID == _capId)
					return i.second->m_enabled ? i.second->interpret(_t, _r) : true;
		}
		else
		{
			for (auto const& i : m_capabilities)
				if (_t >= (int)i.second->m_idOffset && _t - i.second->m_idOffset < i.second->hostCapability()->messageCount())
					return i.second->m_enabled ? i.second->interpret(_t - i.second->m_idOffset, _r) : true;
		}

		return false;
	}
	catch (std::exception const& _e)
	{
		LOG(WARNING) << "Exception caught in p2p::Session::interpret(): " << _e.what() << ". PacketType: " << _t << ". RLP: " << _r;
		disconnect(BadProtocol);
		return true;
	}
	return true;
}

bool Session::interpret(PacketType _t, RLP const& _r)
{
	switch (_t)
	{
	case DisconnectPacket:
	{
		string reason = "Unspecified";
		auto r = (DisconnectReason)_r[0].toInt<int>();
		if (!_r[0].isInt())
			drop(BadProtocol);
		else
		{
			reason = reasonOf(r);
			LOG(WARNING) << "Disconnect (reason: " << reason << ")";
			drop(DisconnectRequested);
		}
		break;
	}
	case PingPacket:
	{
		LOG(TRACE) << "Recv Ping " << m_info.id.abridged();
		RLPStream s;
		sealAndSend(prep(s, PongPacket), 0);
		break;
	}
	case PongPacket:
		DEV_GUARDED(x_info)
		{
			m_info.lastPing = std::chrono::steady_clock::now() - m_ping;
			LOG(TRACE) << "Recv Pong Latency: " << chrono::duration_cast<chrono::milliseconds>(m_info.lastPing).count() << " ms" << m_info.id.abridged();
		}
		break;
	case GetAnnouncementHashPacket:
	{
		break;
	}
	case AnnouncementPacket:
	{
		break;
	}
	case GetPeersPacket:
	case PeersPacket:
		break;
	default:
		return false;
	}
	return true;
}

void Session::ping()
{
	RLPStream s;
	sealAndSend(prep(s, PingPacket), 0);
	m_ping = std::chrono::steady_clock::now();
}
void Session::announcement(h256 const& _allPeerHash)
{
}

RLPStream& Session::prep(RLPStream& _s, PacketType _id, unsigned _args)
{
	return _s.append((unsigned)_id).appendList(_args);
}

void Session::sealAndSend(RLPStream& _s, uint16_t _protocolID)
{
	bytes b;
	_s.swapOut(b);
	send(move(b), _protocolID);
}

bool Session::checkPacket(bytesConstRef _msg)
{
	if (_msg[0] > 0x7f || _msg.size() < 2)
		return false;
	if (RLP(_msg.cropped(1)).actualSize() + 1 != _msg.size())
		return false;
	return true;
}

void Session::send(bytes&& _msg, uint16_t _protocolID)
{
	if (m_dropped)
		return;

	bytesConstRef msg(&_msg);
	if (!checkPacket(msg))
		LOG(WARNING) << "INVALID PACKET CONSTRUCTED!";

	if (!m_socket->isConnected())
		return;

	bool doWrite = false;
	if (isFramingEnabled())
	{
		DEV_GUARDED(x_framing)
		{
			doWrite = m_encFrames.empty();
			auto f = getFraming(_protocolID);
			if (!f)
				return;

			f->writer.enque(RLPXPacket(_protocolID, msg));
			multiplexAll();
		}

		if (doWrite)
			writeFrames();
	}
	else
	{
		DEV_GUARDED(x_framing)
		{
			PacketType packetType = HelloPacket;
			try {
				bytesConstRef frame(_msg.data(), _msg.size());
				packetType = (PacketType)RLP(frame.cropped(0, 1)).toInt<unsigned>();
			}
			catch(std::exception &e) {
				LOG(TRACE) << "Unknown packetType: " << e.what();
			}
			//RLP r(frame.cropped(1));

			m_writeQueue.push_back(std::move(_msg));
			m_writeTimeQueue.push_back(utcTime());
			m_protocloIDQueue.push_back(packetType);
			doWrite = (m_writeQueue.size() == 1);
		}

		if (doWrite)
			write();
	}
}

void Session::onWrite(boost::system::error_code ec, std::size_t length)
{
	try
	{
		if (m_dropped)
		return;

		unsigned elapsed = (unsigned)(utcTime() - m_start_t);
		if (elapsed >= 1000) {
			auto packetType = m_protocloIDQueue[0];
			LOG(WARNING) << "[NETWORK] msg callback type=" << packetType << ",timecost=" << elapsed << ",len=" << length << ",id=" << id();
		}
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		// must check queue, as write callback can occur following dropped()
		if (ec)
		{
			LOG(WARNING) << "Error sending: " << ec.message();
			drop(TCPError);
			return;
		}

		DEV_GUARDED(x_framing)
		{
			m_writeQueue.pop_front();
			m_protocloIDQueue.pop_front();
			m_writeTimeQueue.pop_front();
			if (m_writeQueue.empty())
				return;
		}
		write();
	}
	catch (exception &e) 
	{
		LOG(ERROR) << "Error:" << e.what();
		drop(TCPError);
		return;
	}
}

void Session::write()
{
	try
	{
		if (m_dropped)
			return;

		bytes const* out = nullptr;
		uint64_t enter_time = 0;
		DEV_GUARDED(x_framing)
		{
			m_io->writeSingleFramePacket(&m_writeQueue[0], m_writeQueue[0]);
			out = &m_writeQueue[0];
			enter_time = m_writeTimeQueue[0];
		}
		
		m_start_t = utcTime();
		uint64_t queue_elapsed = m_start_t > enter_time ? (m_start_t - enter_time) : 0;
		if (queue_elapsed >= 60 * 1000) {
			LOG(WARNING) << "[NETWORK] msg waiting in queue timecost=" << queue_elapsed << ", disconnect...";
			drop(PingTimeout);
			return;
		} else if (queue_elapsed >= 1000) {
			auto packetType = m_protocloIDQueue[0];
			LOG(WARNING) << "[NETWORK] msg waiting in queue packetType=" << packetType << " timecost=" << queue_elapsed;
		}

		auto session = shared_from_this();
		if ((m_socket->getSocketType() == SSL_SOCKET_V1) || (m_socket->getSocketType() == SSL_SOCKET_V2))
		{
			if( m_socket->isConnected())
			{
				m_server->getIOService()->post(
					[ = ] {
						if(m_dropped) {
							return;
						}

						boost::asio::async_write(m_socket->sslref(),
						boost::asio::buffer(*out),
						boost::bind(&Session::onWrite, session, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
					});
			}
			else
			{
				LOG(WARNING) << "Error sending ssl socket is close!" ;
				drop(TCPError);
				return;
			}
		}
		else
		{
			ba::async_write(m_socket->ref(), ba::buffer(*out), boost::bind(&Session::onWrite, session, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
		
	}
	catch (exception &e) 
	{
		LOG(ERROR) << "Error:" << e.what();
		drop(TCPError);
		return;
	}

}

void Session::writeFrames()
{
	bytes const* out = nullptr;
	DEV_GUARDED(x_framing)
	{
		if (m_encFrames.empty())
			return;
		else
			out = &m_encFrames[0];
	}

	m_start_t = utcTime();

	auto asyncWrite = [&](boost::system::error_code ec, std::size_t length)
	{
		unsigned elapsed = (unsigned)(utcTime() - m_start_t);
		if (elapsed >= 10) {
			LOG(WARNING) << "ba::async_write write-time=" << elapsed << ",len=" << length << ",id=" << id();
		}
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		// must check queue, as write callback can occur following dropped()
		if (ec)
		{
			LOG(WARNING) << "Error sending: " << ec.message();
			drop(TCPError);
			
			return;
		}

		DEV_GUARDED(x_framing)
		{
			if (!m_encFrames.empty())
				m_encFrames.pop_front();

			multiplexAll();
			if (m_encFrames.empty())
				return;
		}

		writeFrames();
	};
	if ((m_socket->getSocketType() == SSL_SOCKET_V1) || (m_socket->getSocketType() == SSL_SOCKET_V2))
	{
		ba::async_write(m_socket->sslref(), ba::buffer(*out),  m_strand->wrap(asyncWrite) );
	}
	else
	{
		ba::async_write(m_socket->ref(), ba::buffer(*out), m_strand->wrap(asyncWrite));
	}
	
}

void Session::drop(DisconnectReason _reason)
{
	if (m_dropped)
		return;
	m_dropped = true;

	bi::tcp::socket& socket = m_socket->ref();
	if (m_socket->isConnected())
	try
	{
		boost::system::error_code ec;

		LOG(WARNING) << "Session::Drop Closing " << socket.remote_endpoint(ec) << "(" << reasonOf(_reason) << ")"<<m_peer->address();

		auto sslSocket = m_socket;

		//force close socket after 30 seconds
		auto shutdownTimer = std::make_shared<boost::asio::deadline_timer>(*m_server->getIOService(), boost::posix_time::milliseconds(30000));
		shutdownTimer->async_wait(
				[sslSocket](const boost::system::error_code& error) {
					if (error && error != boost::asio::error::operation_aborted)
					{
						LOG(WARNING) << "shutdown timer error: " << error.message();
						return;
					}

					if(sslSocket->ref().is_open()) {
						LOG(WARNING) << "shutdown timeout, force close";
						sslSocket->ref().close();
					}
				});

		sslSocket->sslref().async_shutdown([sslSocket, shutdownTimer](const boost::system::error_code& error) {
			if(error) {
				LOG(WARNING) << "Error while shutdown the ssl socket: " << error.message();
			}
			shutdownTimer->cancel();

			if(sslSocket->ref().is_open()) {
				sslSocket->ref().close();
			}
		});

		//m_server->getIOService()
	}
	catch (...) {}

	m_peer->m_lastDisconnect = _reason;
	if (_reason == BadProtocol)
	{
		m_peer->m_rating /= 2;
		m_peer->m_score /= 2;
	}
	
#if 0
	if( TCPError == _reason )
		m_server->reconnectNow();
#endif
}

void Session::disconnect(DisconnectReason _reason)
{
	LOG(WARNING) << "Disconnecting (our reason:" << reasonOf(_reason) << ")";

	if (m_socket->isConnected())
	{
		RLPStream s;
		prep(s, DisconnectPacket, 1) << (int)_reason;
		sealAndSend(s, 0);
	}
	drop(_reason);
	
}

void Session::start()
{
	ping();

	if (isFramingEnabled())
		m_strand->post(boost::bind(&Session::doReadFrames,this));//doReadFrames();
	else
		m_strand->post(boost::bind(&Session::doRead,this));//doRead();
}

void Session::doRead()
{
	// ignore packets received while waiting to disconnect.
	if (m_dropped)
		return;
	auto self(shared_from_this());
	m_data.resize(h256::size);

	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t length)
	{
		if( length < 1 )
		{
			doRead();
			return ;
		}

		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		if (!checkRead(h256::size, ec, length))
			return;
		else if (!m_io->authAndDecryptHeader(bytesRef(m_data.data(), length)))
		{
			LOG(WARNING) << "header decrypt failed";
			drop(BadProtocol); // todo: better error
			return;
		}

		uint16_t hProtocolId;
		uint32_t hLength;
		uint8_t hPadding;
		try
		{
			RLPXFrameInfo header(bytesConstRef(m_data.data(), length));
			hProtocolId = header.protocolId;
			hLength = header.length;
			hPadding = header.padding;
		}
		catch (std::exception const& _e)
		{
			//LOG(WARNING) << "Exception decoding frame header RLP:" << _e.what() << bytesConstRef(m_data.data(), h128::size).cropped(3);
			LOG(WARNING) << "Exception decoding frame header RLP:" << _e.what() << bytesConstRef(m_data.data(), h128::size).cropped(3).toString();
			drop(BadProtocol);
			return;
		}

		/// read padded frame and mac
		auto tlen = hLength + hPadding + h128::size;
		m_data.resize(tlen);

		auto _asyncRead = [this, self, hLength, hProtocolId, tlen](boost::system::error_code ec, std::size_t length)
		{
			
			ThreadContext tc(info().id.abridged());
			ThreadContext tc2(info().clientVersion);
			if (!checkRead(tlen, ec, length))
				return;
			else if (!m_io->authAndDecryptFrame(bytesRef(m_data.data(), tlen)))
			{
				LOG(WARNING) << "frame decrypt failed";
				drop(BadProtocol); // todo: better error
				return;
			}

			bytesConstRef frame(m_data.data(), hLength);
			if (!checkPacket(frame))
			{
				LOG(WARNING) << "Received " << frame.size() << ": " << toHex(frame) << "\n";
				LOG(WARNING) << "INVALID MESSAGE RECEIVED";
				disconnect(BadProtocol);
				return;
			}
			else
			{
				auto packetType = (PacketType)RLP(frame.cropped(0, 1)).toInt<unsigned>();
				RLP r(frame.cropped(1));
				bool ok = readPacket(hProtocolId, packetType, r);
				(void)ok;

				if (!ok)
					LOG(WARNING) << "Couldn't interpret packet." << RLP(r);

			}
			doRead();
		};

		if ((m_socket->getSocketType() == SSL_SOCKET_V1) || (m_socket->getSocketType() == SSL_SOCKET_V2))
		{
			if( m_socket->isConnected() )
				ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, tlen),  m_strand->wrap(_asyncRead));
			else
			{
				LOG(WARNING) << "Error Reading ssl socket is close!" ;
				drop(TCPError);
				return;
			}
		}
		else
		{
			ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, tlen), m_strand->wrap(_asyncRead));
		}
		
	};

	if ((m_socket->getSocketType() == SSL_SOCKET_V1) || (m_socket->getSocketType() == SSL_SOCKET_V2))
	{
		if( m_socket->isConnected() )
			ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, h256::size),  m_strand->wrap(asyncRead) );
		else
		{
			LOG(WARNING) << "Error Reading ssl socket is close!" ;
			drop(TCPError);
			return;
		}
	}
	else
	{
		ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, h256::size), m_strand->wrap(asyncRead));
	}
	
}

bool Session::checkRead(std::size_t _expected, boost::system::error_code _ec, std::size_t _length)
{
	if (_ec && _ec.category() != boost::asio::error::get_misc_category() && _ec.value() != boost::asio::error::eof)
	{
		LOG(WARNING) << "Error reading: " << _ec.message();
		drop(TCPError);
		
		return false;
	}
	else if (_ec && _length < _expected)
	{
		LOG(WARNING) << "Error reading - Abrupt peer disconnect: " << _ec.message()<<","<<_expected<<","<<_length;
		repMan().noteRude(*this);
		drop(TCPError);
		
		return false;
	}
	else if (_length != _expected)
	{
		// with static m_data-sized buffer this shouldn't happen unless there's a regression
		// sec recommends checking anyways (instead of assert)
		LOG(WARNING) << "Error reading - TCP read buffer length differs from expected frame size."<<_expected<<","<<_length;
		disconnect(UserReason);
		return false;
	}

	return true;
}

void Session::doReadFrames()
{
	if (m_dropped)
		return; // ignore packets received while waiting to disconnect

	auto self(shared_from_this());
	m_data.resize(h256::size);

	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t length)
	{
		if( length < 1 )
		{
			doReadFrames();
			return ;
		}
		ThreadContext tc(info().id.abridged());
		ThreadContext tc2(info().clientVersion);
		if (!checkRead(h256::size, ec, length))
			return;

		DEV_GUARDED(x_framing)
		{
			if (!m_io->authAndDecryptHeader(bytesRef(m_data.data(), length)))
			{
				LOG(WARNING) << "header decrypt failed";
				drop(BadProtocol); // todo: better error
				return;
			}
		}

		bytesConstRef rawHeader(m_data.data(), length);
		try
		{
			RLPXFrameInfo tmpHeader(rawHeader);
		}
		catch (std::exception const& _e)
		{
			LOG(WARNING) << "Exception decoding frame header RLP:" << _e.what() ;//<< bytesConstRef(m_data.data(), h128::size).cropped(3);
			drop(BadProtocol);
			return;
		}

		RLPXFrameInfo header(rawHeader);
		auto tlen = header.length + header.padding + h128::size; // padded frame and mac
		m_data.resize(tlen);
		auto _asyncRead = [this, self, tlen, header](boost::system::error_code ec, std::size_t length)
		{
			
			ThreadContext tc(info().id.abridged());
			ThreadContext tc2(info().clientVersion);
			if (!checkRead(tlen, ec, length))
				return;

			bytesRef frame(m_data.data(), tlen);
			vector<RLPXPacket> px;
			DEV_GUARDED(x_framing)
			{
				auto f = getFraming(header.protocolId);
				if (!f)
				{
					LOG(WARNING) << "Unknown subprotocol " << header.protocolId;
					drop(BadProtocol);
					return;
				}

				auto v = f->reader.demux(*m_io, header, frame);
				px.swap(v);
			}

			for (RLPXPacket& p : px)
			{
				PacketType packetType = (PacketType)RLP(p.type()).toInt<unsigned>(RLP::AllowNonCanon);
				bool ok = readPacket(header.protocolId, packetType, RLP(p.data()));
				ok = true;
				(void)ok;
			}
			doReadFrames();
		};
		if ((m_socket->getSocketType() == SSL_SOCKET_V1) || (m_socket->getSocketType() == SSL_SOCKET_V2))
		{
			ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, tlen), m_strand->wrap( _asyncRead) );
		}
		else
		{
			ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, tlen), m_strand->wrap( _asyncRead));
		}
		
	};
	if ((m_socket->getSocketType() == SSL_SOCKET_V1) || (m_socket->getSocketType() == SSL_SOCKET_V2))
	{
		ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, h256::size),  m_strand->wrap(asyncRead) );
	}
	else
	{
		ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, h256::size), m_strand->wrap(asyncRead));
	}
	
}

std::shared_ptr<Session::Framing> Session::getFraming(uint16_t _protocolID)
{
	if (m_framing.find(_protocolID) == m_framing.end())
		return nullptr;
	else
		return m_framing[_protocolID];
}

void Session::registerCapability(CapDesc const& _desc, std::shared_ptr<Capability> _p)
{
	DEV_GUARDED(x_framing)
	{
		m_capabilities[_desc] = _p;
	}
}

void Session::registerFraming(uint16_t _id)
{
	DEV_GUARDED(x_framing)
	{
		if (m_framing.find(_id) == m_framing.end())
		{
			std::shared_ptr<Session::Framing> f(new Session::Framing(_id));
			m_framing[_id] = f;
		}
	}
}

void Session::multiplexAll()
{
	for (auto& f : m_framing)
		f.second->writer.mux(*m_io, maxFrameSize(), m_encFrames);
}

CABaseData* Session::getCABaseData()
{
	return m_CABaseData;
}

void Session::saveCABaseData(CABaseData* baseData)
{
	m_CABaseData = baseData;
}
