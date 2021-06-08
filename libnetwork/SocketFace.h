/*
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
 */
/**
 * @brief: Socket inteface
 * @file SocketFace.h
 * @author yujiechen
 * @date 2018-09-17
 */

#pragma once
#include "Common.h"
#include <boost/beast.hpp>
namespace dev
{
namespace network
{
class SocketFace
{
public:
    SocketFace() = default;

    virtual ~SocketFace(){};
    virtual bool isConnected() const = 0;
    virtual void close() = 0;
    virtual bi::tcp::endpoint remoteEndpoint(
        boost::system::error_code ec = boost::system::error_code()) = 0;
    virtual bi::tcp::endpoint localEndpoint(
        boost::system::error_code ec = boost::system::error_code()) = 0;

    virtual bi::tcp::socket& ref() = 0;
    virtual ba::ssl::stream<bi::tcp::socket>& sslref() = 0;

    virtual const NodeIPEndpoint& nodeIPEndpoint() const = 0;
    virtual void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) = 0;
};
}  // namespace network
}  // namespace dev
