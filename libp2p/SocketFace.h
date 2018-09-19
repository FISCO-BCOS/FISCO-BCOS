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
/**
 * @brief: Socket inteface
 * @file SocketFace.h
 * @author yujiechen
 * @date 2018-09-17
 */

#pragma once
#include "Common.h"
namespace dev
{
namespace p2p
{
class SocketFace
{
public:
    SocketFace() = default;

    virtual ~SocketFace(){};
    virtual bool isConnected() const = 0;
    virtual void close() = 0;
    virtual bi::tcp::endpoint remoteEndpoint() = 0;

    virtual bi::tcp::socket& ref() = 0;

    virtual ba::ssl::stream<bi::tcp::socket>& sslref() = 0;
    virtual const NodeIPEndpoint& nodeIPEndpoint() const = 0;
    virtual void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) = 0;
    virtual boost::asio::ip::tcp::endpoint remote_endpoint() = 0;
};
}  // namespace p2p
}  // namespace dev
