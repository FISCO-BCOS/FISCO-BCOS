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
/** @file RLPXSocket.h
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2015
 * @author toxotguo
 * @date 2018
 */

#pragma once
#include<iostream>
using namespace std;
#include "Common.h"
#include <libdevcore/easylog.h>
#include <libdevcore/FileSystem.h>
#include <boost/filesystem.hpp>

namespace dev
{
namespace p2p
{

/**
 * @brief Shared pointer wrapper for ASIO TCP socket.
 *
 * Thread Safety
 * Distinct Objects: Safe.
 * Shared objects: Unsafe.
 * * an instance method must not be called concurrently
 */
class RLPXSocketApi
{
	public:
		virtual bool isConnected() const = 0;
		virtual void close() = 0;
		virtual bi::tcp::endpoint remoteEndpoint()  = 0;
		virtual int getSocketType() = 0;
		virtual bi::tcp::socket& ref() = 0;
		virtual ba::ssl::stream<bi::tcp::socket>& sslref() = 0;
};

class RLPXSocket: public RLPXSocketApi,public std::enable_shared_from_this<RLPXSocket>
{
public:
	RLPXSocket(ba::io_service& _ioService)
	{
		if (dev::getSSL() == SSL_SOCKET_V1)
		{
			LOG(DEBUG)<<"RLPXSocket::m_socketType:1";
			m_socketType = SSL_SOCKET_V1;
			ba::ssl::context sslContext(ba::ssl::context::tlsv12);
			string certData = asString( contents( getDataDir() + "/ca.crt") );
			if (certData != "")
			{
				sslContext.add_certificate_authority(boost::asio::const_buffer(certData.c_str(), certData.size()));
			}
			else
			{
				LOG(ERROR)<<"Get ca.crt File Err......................";
			}
			certData = asString( contents( getDataDir() + "/server.crt") );
			if (certData != "")
			{
				sslContext.use_certificate_chain(boost::asio::const_buffer(certData.c_str(), certData.size()));
			}
			else
			{
				LOG(ERROR)<<"Get server.crt File Err......................";
			}
			certData = asString( contents( getDataDir() + "/server.key") );
			if (certData != "")
			{
				sslContext.use_private_key(boost::asio::const_buffer(certData.c_str(), certData.size()),ba::ssl::context_base::pem);
			}
			else
			{
				LOG(ERROR)<<"Get server.key File Err......................";
			}
			m_sslsocket = std::make_shared<ba::ssl::stream<bi::tcp::socket> >(_ioService,sslContext);
		}
		else
		{
			LOG(DEBUG)<<"RLPXSocket::m_socketType:0";
			m_socketType = BASE_SOCKET;
			m_socket = std::make_shared<bi::tcp::socket>(_ioService);
		}
	}
	~RLPXSocket() 
	{ 
		close(); 
	}
	
	bool isConnected() const 
	{ 
		if (m_socketType == SSL_SOCKET_V1)
		{
			return m_sslsocket->lowest_layer().is_open();
		}
		return m_socket->is_open(); 
	}
	void close() 
	{ 
		try 
		{ 
			boost::system::error_code ec;
			if (m_socketType == SSL_SOCKET_V1)
			{
				m_sslsocket->lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
				if (m_sslsocket->lowest_layer().is_open())
					m_sslsocket->lowest_layer().close();
			}
			else
			{
				m_socket->shutdown(bi::tcp::socket::shutdown_both, ec);
				if (m_socket->is_open()) 
					m_socket->close();
			}	 
		} 
		catch (...){} 
	}
	bi::tcp::endpoint remoteEndpoint() 
	{ 
		boost::system::error_code ec; 
		if (m_socketType == SSL_SOCKET_V1)
		{
			return m_sslsocket->lowest_layer().remote_endpoint(ec);
		}
		return m_socket->remote_endpoint(ec);
	}

	int getSocketType()
	{
		return m_socketType;
	}

	bi::tcp::socket& ref() 
	{ 
		if (m_socketType == SSL_SOCKET_V1)
		{
			return m_sslsocket->next_layer();
		}
		return *m_socket;
	}

	ba::ssl::stream<bi::tcp::socket>& sslref()
	{
		return *m_sslsocket;
	}
protected:
	int m_socketType;
	std::shared_ptr<bi::tcp::socket> m_socket;
	std::shared_ptr<ba::ssl::stream<bi::tcp::socket> > m_sslsocket;
};

}
}
