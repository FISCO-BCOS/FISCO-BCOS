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
/** @file RLPXHandshakeSSL.cpp
 * @author toxotguo
 * @date 2018
 */

#include "Host.h"
#include "Session.h"
#include "Peer.h"
#include "RLPxHandshakeSSL.h"
#include "libethereum/NodeConnParamsManager.h"
#include "libdevcrypto/Rsa.h"
#include "libdevcore/Hash.h"
#include <fstream>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::crypto;



void RLPXHandshakeSSL::cancel()
{
	m_cancel = true;
	m_idleTimer.cancel();
	m_socket->close();
	m_io.reset();
}

void RLPXHandshakeSSL::error()
{
	auto connected = m_socket->isConnected();
	if (connected && !m_socket->remoteEndpoint().address().is_unspecified())
		LOG(INFO) << "Disconnecting " << m_socket->remoteEndpoint() << " (Handshake Failed)";
	else
		LOG(INFO) << "Handshake Failed (Connection reset by peer)";

	cancel();
}

void RLPXHandshakeSSL::transition(boost::system::error_code _ech)
{
	LOG(INFO) << "Start Handshake";
	// reset timeout
	m_idleTimer.cancel();

	if (_ech || m_nextState == Error || m_cancel)
	{
		LOG(WARNING) << "Handshake Failed (I/O Error:" << _ech.message() << ")";
		return error();
	}

	auto self(shared_from_this());
	assert(m_nextState != StartSession);
	m_idleTimer.expires_from_now(c_timeout);
	m_idleTimer.async_wait(m_strand->wrap([this, self](boost::system::error_code const & _ec)
	{
		if (!_ec)
		{
			if (!m_socket->remoteEndpoint().address().is_unspecified())
				LOG(INFO) << "Disconnecting " << m_socket->remoteEndpoint() << " (Handshake Timeout)";
			cancel();
		}
	}));

	if (m_nextState == WriteHello)
	{
		m_nextState = ReadHello;
		LOG(INFO) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ") << "sending capabilities handshake";

		m_io.reset(new RLPXFrameCoder(*this));

		
		RLPStream s;
		h256 hash;
        if (!NodeConnManagerSingleton::GetInstance().nodeInfoHash(hash))
            return;
		s.append((unsigned)HelloPacket).appendList(6)
		        << dev::p2p::c_protocolVersion
		        << m_host->getClientVersion()
		        << m_host->caps()
		        << m_host->listenPort()
		        << m_host->id()
				<< hash;

		bytes packet;
		s.swapOut(packet);
		m_io->writeSingleFramePacket(&packet, m_handshakeOutBuffer);
		auto asyncWrite = [this, self](boost::system::error_code ec, std::size_t)
		{
			transition(ec);
		};
		ba::async_write(m_socket->sslref(), ba::buffer(m_handshakeOutBuffer), m_strand->wrap(asyncWrite) );
		
	}
	else if (m_nextState == ReadHello)
	{
		// Authenticate and decrypt initial hello frame with initial RLPXFrameCoder
		// and request m_host to start session.
		m_nextState = StartSession;

		// read frame header
		unsigned const handshakeSize = 32;
		m_handshakeInBuffer.resize(handshakeSize);

		auto asyncRead = [this, self](boost::system::error_code ec, std::size_t)
		{
			if (ec)
				transition(ec);
			else
			{
				if (!m_io)
				{
					LOG(WARNING) << "Internal error in handshake: RLPXFrameCoder disappeared.";
					m_nextState = Error;
					transition();
					return;

				}
				/// authenticate and decrypt header
				if (!m_io->authAndDecryptHeader(bytesRef(m_handshakeInBuffer.data(), m_handshakeInBuffer.size())))
				{
					m_nextState = Error;
					transition();
					return;
				}

				LOG(INFO) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ") << "recvd hello header";

				/// check frame size
				bytes& header = m_handshakeInBuffer;
				uint32_t frameSize = (uint32_t)(header[2]) | (uint32_t)(header[1]) << 8 | (uint32_t)(header[0]) << 16;
				if (frameSize > 1024)
				{
					// all future frames: 16777216
					LOG(WARNING) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ") << "hello frame is too large" << frameSize;
					m_nextState = Error;
					transition();
					return;
				}

				/// rlp of header has protocol-type, sequence-id[, total-packet-size]
				bytes headerRLP(header.size() - 3 - h128::size);	// this is always 32 - 3 - 16 = 13. wtf?
				bytesConstRef(&header).cropped(3).copyTo(&headerRLP);

				/// read padded frame and mac
				m_handshakeInBuffer.resize(frameSize + ((16 - (frameSize % 16)) % 16) + h128::size);
				auto _asyncRead = [this, self, headerRLP](boost::system::error_code ec, std::size_t)
				{
					m_idleTimer.cancel();

					if (ec)
						transition(ec);
					else
					{
						if (!m_io)
						{
							LOG(WARNING) << "Internal error in handshake: RLPXFrameCoder disappeared.";
							m_nextState = Error;
							transition();
							return;

						}
						bytesRef frame(&m_handshakeInBuffer);
						if (!m_io->authAndDecryptFrame(frame))
						{
							LOG(WARNING) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ") << "hello frame: decrypt failed";
							m_nextState = Error;
							transition();
							return;
						}

						PacketType packetType = frame[0] == 0x80 ? HelloPacket : (PacketType)frame[0];
						if (packetType != HelloPacket)
						{
							LOG(WARNING) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ") << "hello frame: invalid packet type:" << packetType;
							m_nextState = Error;
							transition();
							return;
						}

						LOG(INFO) << (m_originated ? "p2p.connect.egress " : "p2p.connect.ingress ") << "hello frame: success. starting session.";
						try
						{
							RLP rlp(frame.cropped(1), RLP::ThrowOnFail | RLP::FailIfTooSmall);
							m_host->startPeerSession( rlp, move(m_io), m_socket, m_sendBaseData);
						}
						catch (std::exception const& _e)
						{
							LOG(WARNING) << "Handshake causing an exception:" << _e.what();
							m_nextState = Error;
							transition();
						}
					}
				};
				ba::async_read(m_socket->sslref(), boost::asio::buffer(m_handshakeInBuffer, m_handshakeInBuffer.size()), m_strand->wrap( _asyncRead) );
				
			}
		};

		ba::async_read(m_socket->sslref(), boost::asio::buffer(m_handshakeInBuffer, handshakeSize), m_strand->wrap( asyncRead) );
		
	}
}