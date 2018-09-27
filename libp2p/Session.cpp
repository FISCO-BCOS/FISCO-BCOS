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

Session::Session(Host* _h, std::shared_ptr<RLPXSocket> const& _s, std::shared_ptr<Peer> const& _n, PeerSessionInfo _info):
	m_server(_h),
	m_socket(_s),
	m_peer(_n),
	m_info(_info),
	m_ping(chrono::steady_clock::time_point::max())
{
	//registerFraming(0);
	m_peer->m_lastDisconnect = NoDisconnect;
	m_lastReceived = m_connect = chrono::steady_clock::now();
	DEV_GUARDED(x_info)
	m_info.socketId = m_socket->ref().lowest_layer().native_handle();
}

Session::~Session()
{
	ThreadContext tc(info().id.abridged());
	ThreadContext tc2(info().clientVersion);
	LOG(INFO) << "Closing peer session :-(";
	m_peer->m_lastConnected = m_peer->m_lastAttempted - chrono::seconds(1);

	// Read-chain finished for one reason or another.
	for (auto& i : m_capabilities)
		i.second.reset();

	try
	{
		if (m_socket->ref().lowest_layer().is_open())
		{
			boost::system::error_code ec;
			m_socket->ref().lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			m_socket->ref().lowest_layer().close();
		}
	}
	catch (...) {}
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

// 收到数据包
bool Session::readPacket(uint16_t _capId, PacketType _t, RLP const& _r)
{
	m_lastReceived = chrono::steady_clock::now();
	//LOG(INFO) << _t << _r;
	try // Generic try-catch block designed to capture RLP format errors - TODO: give decent diagnostics, make a bit more specific over what is caught.
	{
		// v4 frame headers are useless, offset packet type used
		// v5 protocol type is in header, packet type not offset
		if (_capId == 0 && _t < UserPacket)
			return interpret(_t, _r);

		//if (isFramingEnabled())
		if(false)
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
		LOG(INFO) << "Recv Ping " << m_info.id;
		RLPStream s;
		sealAndSend(prep(s, PongPacket), 0);
		break;
	}
	case PongPacket:
		DEV_GUARDED(x_info)
		{
			m_info.lastPing = std::chrono::steady_clock::now() - m_ping;
			LOG(INFO) << "Recv Pong Latency: " << chrono::duration_cast<chrono::milliseconds>(m_info.lastPing).count() << " ms" << m_info.id;
		}
		break;
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

RLPStream& Session::prep(RLPStream& _s, PacketType _id, unsigned _args)
{
	return _s.append((unsigned)_id).appendList(_args);
}

void Session::sealAndSend(RLPStream& _s, uint16_t _protocolID)
{
	std::shared_ptr<bytes> b = std::make_shared<bytes>();
	_s.swapOut(*b);
	send(b, _protocolID);
}

bool Session::checkPacket(bytesConstRef _msg)
{
	if (_msg[0] > 0x7f || _msg.size() < 2)
		return false;
	if (RLP(_msg.cropped(1)).actualSize() + 1 != _msg.size())
		return false;
	return true;
}

void Session::send(std::shared_ptr<bytes> _msg, uint16_t _protocolID)
{
	bytesConstRef msg(_msg.get());
	//LOG(TRACE) << RLP(msg.cropped(1));
	if (!checkPacket(msg))
		LOG(WARNING) << "INVALID PACKET CONSTRUCTED!";

	if (!m_socket->ref().lowest_layer().is_open())
		return;

	bool doWrite = false;
	DEV_GUARDED(x_framing)
	{
		_writeQueue.push(boost::make_tuple(_msg, _protocolID, utcTime()));
		doWrite = (_writeQueue.size() == 1);

#if 0
		m_writeQueue.push_back(std::move(_msg));
		_protocolIDQueue.push_back(_protocolID);
		m_writeTimeQueue.push_back(utcTime());
		doWrite = (m_writeQueue.size() == 1);
#endif
	}

	if (doWrite)
		write();
}

void Session::write()
{
	try {
		boost::tuple<std::shared_ptr<bytes>, uint16_t, u256> task;

		u256 enter_time = 0;
		DEV_GUARDED(x_framing)
		{
			task = _writeQueue.top();

			Header header;
			uint32_t length =  sizeof(Header) + boost::get<0>(task)->size();
			header.length = htonl(length);
			header.protocolID = htonl(boost::get<1>(task));

			std::shared_ptr<bytes> out = boost::get<0>(task);
			out->insert(out->begin(), (byte*)&header, ((byte*)&header) + sizeof(Header));

			enter_time = boost::get<2>(task);
		}

		auto self(shared_from_this());
		m_start_t = utcTime();
		unsigned queue_elapsed = (unsigned)(m_start_t - enter_time);
		if (queue_elapsed > 10) {
			LOG(WARNING) << "Session::write queue-time=" << queue_elapsed;
		}

		auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t length)
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
				LOG(DEBUG) << "Error sending: " << ec.message();
				drop(TCPError);
				return;
			}

			DEV_GUARDED(x_framing)
			{
				_writeQueue.pop();

				if(_writeQueue.empty()) {
					return;
				}
			}
			write();
		};

		if(m_socket->ref().lowest_layer().is_open())
		{
			m_server->getIOService()->post(
				[ = ] {
					boost::asio::async_write(m_socket->ref(),
					boost::asio::buffer(*(boost::get<0>(task))),
					asyncWrite);
				});
		}
		else
		{
			LOG(WARNING) << "Error sending ssl socket is close!" ;
			drop(TCPError);
			return;
		}
	}
	catch(std::exception &e) {
		LOG(ERROR) << "Error while write" << e.what();
		drop(TCPError);
		return;
	}
}

void Session::drop(DisconnectReason _reason)
{
	if (m_dropped)
		return;

	if (m_socket->ref().lowest_layer().is_open())
		try
		{
			boost::system::error_code ec;
			LOG(DEBUG) << "Closing " << m_socket->ref().lowest_layer().remote_endpoint(ec) << "(" << reasonOf(_reason) << ")";
			m_socket->ref().lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
			m_socket->ref().lowest_layer().close();
		}
		catch (...) {}

	m_peer->m_lastDisconnect = _reason;
	if (_reason == BadProtocol)
	{
		m_peer->m_rating /= 2;
		m_peer->m_score /= 2;
	}
	m_dropped = true;
}

void Session::disconnect(DisconnectReason _reason)
{
	LOG(DEBUG) << "Disconnecting (our reason:" << reasonOf(_reason) << ")";

	if (m_socket->ref().lowest_layer().is_open())
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

	doRead();
}

void Session::doRead()
{
	// ignore packets received while waiting to disconnect.
	if (m_dropped)
		return;
	auto self(shared_from_this());
	m_data.resize(sizeof(Header));

	auto asyncRead = [this, self](boost::system::error_code ec, std::size_t length)
	{
		if (!checkRead(sizeof(Header), ec, length))
			return;

		Header *header = (Header*)m_data.data();

		uint32_t hLength = ntohl(header->length);
		uint32_t protocolID = ntohl(header->protocolID);

		/// read padded frame and mac
		m_data.clear();
		m_data.resize(hLength - sizeof(Header));

		auto _asyncRead = [this, self, hLength, protocolID](boost::system::error_code ec, std::size_t length)
		{
			ThreadContext tc(info().id.abridged());
			ThreadContext tc2(info().clientVersion);
			if (!checkRead(hLength - sizeof(Header), ec, length))
				return;

			bytesConstRef frame(m_data.data(), length);
			auto packetType = (PacketType)RLP(frame.cropped(0, 1)).toInt<unsigned>();

			RLP r(frame.cropped(1));
			bool ok = readPacket((unsigned short)protocolID, packetType, r);
			if (!ok)
				LOG(WARNING) << "Couldn't interpret packet." << RLP(r);

			doRead();
		};

		ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, hLength - sizeof(Header)), _asyncRead);
	};

	ba::async_read(m_socket->ref(), boost::asio::buffer(m_data, h256::size), asyncRead);
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
		LOG(WARNING) << "Error reading - Abrupt peer disconnect: " << _ec.message();
		drop(TCPError);
		return false;
	}
	else if (_length != _expected)
	{
		// with static m_data-sized buffer this shouldn't happen unless there's a regression
		// sec recommends checking anyways (instead of assert)
		LOG(WARNING) << "Error reading - TCP read buffer length differs from expected frame size.";
		disconnect(UserReason);
		return false;
	}

	return true;
}

void Session::registerCapability(CapDesc const& _desc, std::shared_ptr<Capability> _p)
{
	DEV_GUARDED(x_framing)
	{
		m_capabilities[_desc] = _p;
	}
}
