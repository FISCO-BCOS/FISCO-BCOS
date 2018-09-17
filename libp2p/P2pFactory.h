/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief: intefaces for p2p modules(for unittest)
 *
 * @file P2pInterfaces.h
 * @author: yujiechen
 * @date 2018-09-17
 */
#pragma once
#include "Session.h"
#include "SessionFace.h"
#include "Socket.h"
#include "SocketFace.h"
namespace dev
{
namespace p2p
{
class Host;
class SocketFactory
{
public:
    virtual std::shared_ptr<SocketFace> create_socket(
        ba::io_service& _ioService, NodeIPEndpoint _nodeIPEndpoint = NodeIPEndpoint())
    {
        /// default create Session
        std::shared_ptr<SocketFace> m_socket =
            std::make_shared<Socket>(_ioService, _nodeIPEndpoint);
        return m_socket;
    }
};

class SessionFactory
{
public:
    virtual std::shared_ptr<SessionFace> create_session(Host* _server,
        std::shared_ptr<SocketFace> const& _socket, std::shared_ptr<Peer> const& _peer,
        PeerSessionInfo _info)
    {
        std::shared_ptr<SessionFace> m_session =
            std::make_shared<Session>(_server, _socket, _peer, _info);
        return m_session;
    }
};

}  // namespace p2p
}  // namespace dev
