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

#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/easylog.h>
#include <chrono>
using namespace std;
using namespace dev;
using namespace dev::p2p;

Session::Session(Host* _server, std::shared_ptr<RLPXSocket> const& _s,
    std::shared_ptr<Peer> const& _n, PeerSessionInfo _info)
  : m_server(_server),
    m_socket(_s),
    m_peer(_n),
    m_info(_info),
    m_ping(chrono::steady_clock::time_point::max())
{
    m_peer->m_lastDisconnect = NoDisconnect;
    m_lastReceived = m_connect = chrono::steady_clock::now();
    DEV_GUARDED(x_info)
    m_info.socketId = m_socket->ref().native_handle();


    m_strand = _server->strand();
}

Session::~Session()
{
    ThreadContext tc(info().id.abridged());
    ThreadContext tc2(info().clientVersion);
    LOG(INFO) << "Closing peer session :-(";
    m_peer->m_lastConnected = m_peer->m_lastAttempted - chrono::seconds(1);

    try
    {
        bi::tcp::socket& socket = m_socket->ref();
        if (m_socket->isConnected())
        {
            boost::system::error_code ec;

            // shutdown may block servals seconds - morebtcg
            // socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close();
        }
    }
    catch (...)
    {
        LOG(ERROR) << "Deconstruct Session exception";
    }
}

NodeID Session::id() const
{
    return m_peer ? m_peer->id() : NodeID();
}

template <class T>
vector<T> randomSelection(vector<T> const& _t, unsigned _n)
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
// @param: _protocolID indicates message type, such as connection msg, sync msg, pbft msg, and so
// on.
bool Session::readPacket(uint16_t _protocolID, PacketType _t, RLP const& _r)
{
    m_lastReceived = chrono::steady_clock::now();
    try  // Generic try-catch block designed to capture RLP format errors - TODO: give decent
         // diagnostics, make a bit more specific over what is caught.
    {
        if (_protocolID == 0 && _t < UserPacket)
        {
            return interpret(_t, _r);
        }

        // TODO(wheatli): process the other message

        return false;
    }
    catch (std::exception const& _e)
    {
        LOG(WARNING) << "Exception caught in p2p::Session::interpret(): " << _e.what()
                     << ". PacketType: " << _t << ". RLP: " << _r;
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
            LOG(INFO) << "Recv Pong Latency: "
                      << chrono::duration_cast<chrono::milliseconds>(m_info.lastPing).count()
                      << " ms" << m_info.id;
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
    // LOG(TRACE) << RLP(msg.cropped(1));
    if (!checkPacket(msg))
        LOG(WARNING) << "INVALID PACKET CONSTRUCTED!";

    if (!m_socket->isConnected())
        return;

    bool doWrite = false;
    DEV_GUARDED(x_framing)
    {
        m_writeQueue.push(boost::make_tuple(_msg, _protocolID, utcTime()));
        doWrite = (m_writeQueue.size() == 1);
    }
    if (doWrite)
        write();
}

void Session::onWrite(boost::system::error_code ec, std::size_t length)
{
    try
    {
        unsigned elapsed = (unsigned)(utcTime() - m_start_t);
        if (elapsed >= 10)
        {
            LOG(WARNING) << "ba::async_write write-time=" << elapsed << ",len=" << length
                         << ",id=" << id();
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
            m_writeQueue.pop();
            if (m_writeQueue.empty())
            {
                return;
            }
        }
        write();
    }
    catch (exception& e)
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
        boost::tuple<std::shared_ptr<bytes>, uint16_t, u256> task;
        u256 enter_time = 0;
        DEV_GUARDED(x_framing)
        {
            task = m_writeQueue.top();
            Header header;
            uint32_t length = sizeof(Header) + boost::get<0>(task)->size();
            header.length = htonl(length);
            header.protocolID = htonl(boost::get<1>(task));
            std::shared_ptr<bytes> out = boost::get<0>(task);
            out->insert(out->begin(), (byte*)&header, ((byte*)&header) + sizeof(Header));
            enter_time = boost::get<2>(task);
        }

        auto self(shared_from_this());
        m_start_t = utcTime();
        unsigned queue_elapsed = (unsigned)(m_start_t - enter_time);
        if (queue_elapsed > 10)
        {
            LOG(WARNING) << "Session::write queue-time=" << queue_elapsed;
        }

        auto session = shared_from_this();

        if (m_socket->isConnected())
        {
            m_server->ioService()->post([=] {
                boost::asio::async_write(m_socket->sslref(),
                    boost::asio::buffer(*(boost::get<0>(task))),
                    boost::bind(&Session::onWrite, session, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
            });
        }
        else
        {
            LOG(WARNING) << "Error sending ssl socket is close!";
            drop(TCPError);
            return;
        }
    }
    catch (exception& e)
    {
        LOG(ERROR) << "Error:" << e.what();
        drop(TCPError);
        return;
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

            // shutdown may block servals seconds - morebtcg
            // socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            LOG(WARNING) << "Closing " << socket.remote_endpoint(ec) << "(" << reasonOf(_reason)
                         << ")" << m_peer->address() << "," << ec.message();

            socket.close();
        }
        catch (...)
        {
        }
    m_peer->m_lastDisconnect = _reason;
    if (TCPError == _reason)
        m_server->reconnectNow();
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
    m_strand->post(boost::bind(&Session::doRead, this));  // doRead();
}

void Session::doRead()
{
    // ignore packets received while waiting to disconnect.
    if (m_dropped)
        return;
    auto self(shared_from_this());
    m_data.resize(sizeof(Header));

    auto asyncRead = [this, self](boost::system::error_code ec, std::size_t length) {
        if (!checkRead(sizeof(Header), ec, length))
            return;
        Header* header = (Header*)m_data.data();
        uint32_t hLength = ntohl(header->length);
        uint32_t protocolID = ntohl(header->protocolID);

        /// read padded frame and mac
        m_data.clear();
        m_data.resize(hLength - sizeof(Header));

        auto _asyncRead = [this, self, hLength, protocolID](
                              boost::system::error_code ec, std::size_t length) {
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
        if (m_socket->isConnected())
            ba::async_read(m_socket->sslref(),
                boost::asio::buffer(m_data, hLength - sizeof(Header)), m_strand->wrap(_asyncRead));
        else
        {
            LOG(WARNING) << "Error Reading ssl socket is close!";
            drop(TCPError);
            return;
        }
    };
    if (m_socket->isConnected())
        ba::async_read(m_socket->sslref(), boost::asio::buffer(m_data, sizeof(Header)),
            m_strand->wrap(asyncRead));
    else
    {
        LOG(WARNING) << "Error Reading ssl socket is close!";
        drop(TCPError);
        return;
    }
}

bool Session::checkRead(std::size_t _expected, boost::system::error_code _ec, std::size_t _length)
{
    if (_ec && _ec.category() != boost::asio::error::get_misc_category() &&
        _ec.value() != boost::asio::error::eof)
    {
        LOG(WARNING) << "Error reading: " << _ec.message();
        drop(TCPError);

        return false;
    }
    else if (_ec && _length < _expected)
    {
        LOG(WARNING) << "Error reading - Abrupt peer disconnect: " << _ec.message() << ","
                     << _expected << "," << _length;
        drop(TCPError);

        return false;
    }
    else if (_length != _expected)
    {
        // with static m_data-sized buffer this shouldn't happen unless there's a regression
        // sec recommends checking anyways (instead of assert)
        LOG(WARNING) << "Error reading - TCP read buffer length differs from expected frame size."
                     << _expected << "," << _length;
        disconnect(UserReason);
        return false;
    }

    return true;
}
