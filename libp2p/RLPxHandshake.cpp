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
/** @file RLPXHandshake.cpp
 * @author toxotguo
 * @date 2018
 */

#include "RLPxHandshake.h"
#include "Host.h"
#include "Peer.h"
#include "Session.h"
#include <libdevcrypto/Rsa.h>
#include <fstream>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::crypto;


void RLPXHandshake::cancel()
{
    m_cancel = true;
    m_idleTimer.cancel();
    m_socket->close();
}

void RLPXHandshake::error()
{
    auto connected = m_socket->isConnected();
    if (connected && !m_socket->remoteEndpoint().address().is_unspecified())
        LOG(INFO) << "Disconnecting " << m_socket->remoteEndpoint() << " (Handshake Failed)";
    else
        LOG(INFO) << "Handshake Failed (Connection reset by peer)";

    cancel();
}

void RLPXHandshake::transition(boost::system::error_code _ech)
{
    LOG(INFO) << "Start Handshake";
    // reset timeout
    m_idleTimer.cancel();

    if (_ech || m_nextState == Error || m_cancel)
    {
        LOG(ERROR) << "Handshake Failed (I/O Error:" << _ech.message() << ")";
        return error();
    }

    auto self(shared_from_this());
    assert(m_nextState != StartSession);
    m_idleTimer.expires_from_now(c_timeout);
    m_idleTimer.async_wait(m_strand->wrap([this, self](boost::system::error_code const& _ec) {
        if (!_ec)
        {
            if (!m_socket->remoteEndpoint().address().is_unspecified())
                LOG(INFO) << "Disconnecting " << m_socket->remoteEndpoint()
                          << " (Handshake Timeout)";
            cancel();
        }
    }));

    if (m_nextState == WriteHello)
    {
        m_nextState = ReadHello;
        LOG(INFO) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ")
                  << "sending capabilities handshake";

        RLPStream s;
        s.append((unsigned)HelloPacket).appendList(5)
            << dev::p2p::c_protocolVersion << m_host->getClientVersion() << m_host->caps()
            << m_host->listenPort() << m_host->id();

        bytes packet;
        s.swapOut(packet);
        auto asyncWrite = [this, self](
                              boost::system::error_code ec, std::size_t) { transition(ec); };
        /// add package length
        uint32_t length = htonl(packet.size() + sizeof(uint32_t));
        packet.insert(packet.begin(), (byte*)&length, (byte*)(&length) + sizeof(uint32_t));
        m_handshakeOutBuffer = packet;
        ba::async_write(
            m_socket->sslref(), ba::buffer(m_handshakeOutBuffer), m_strand->wrap(asyncWrite));
        LOG(DEBUG) << "Write hello packet length: " << ntohl(length);
    }
    else if (m_nextState == ReadHello)
    {
        // Authenticate and decrypt initial hello frame with initial RLPXFrameCoder
        // and request m_host to start session.
        m_nextState = StartSession;

        // read frame header
        unsigned const handshakeSize = sizeof(uint32_t);
        m_handshakeInBuffer.resize(handshakeSize);
        auto asyncRead = [this, self](boost::system::error_code ec, std::size_t) {
            if (ec)
                transition(ec);
            else
            {
                uint32_t length = ntohl(*((uint32_t*)m_handshakeInBuffer.data()));

                LOG(DEBUG) << "Read hello packet length: " << length;

                /// read padded frame and mac
                m_handshakeInBuffer.clear();
                m_handshakeInBuffer.resize(length - sizeof(uint32_t));
                auto _asyncRead = [this, self](boost::system::error_code error, std::size_t) {
                    m_idleTimer.cancel();

                    if (error)
                        transition(error);
                    else
                    {
                        LOG(INFO) << (m_originated ? "p2p.connect.egress" : "p2p.connect.ingress")
                                  << "hello frame: success. starting session.";
                        try
                        {
                            // bytesConstRef buffer(m_handshakeInBuffer.data(),
                            // m_handshakeInBuffer.size());
                            RLP rlp(m_handshakeInBuffer,
                                RLP::FailIfTooBig | RLP::ThrowOnFail | RLP::FailIfTooSmall);
                            LOG(DEBUG) << "handshakeInBuffer: " << m_handshakeInBuffer.size()
                                       << ", " << rlp.actualSize() << ", " << rlp.itemCount();

                            LOG(DEBUG)
                                << "Handshake successed, startPeerSession: " << m_remote.hex();

                            m_host->startPeerSession(rlp, m_socket);
                        }
                        catch (std::exception const& _e)
                        {
                            LOG(ERROR) << "Handshake causing an exception:" << _e.what() << " "
                                       << boost::diagnostic_information(_e);
                            m_nextState = Error;
                            transition();
                        }
                    }
                };
                ba::async_read(m_socket->sslref(),
                    boost::asio::buffer(m_handshakeInBuffer, m_handshakeInBuffer.size()),
                    m_strand->wrap(_asyncRead));
            }
        };
        ba::async_read(m_socket->sslref(), boost::asio::buffer(m_handshakeInBuffer, handshakeSize),
            m_strand->wrap(asyncRead));
    }
}