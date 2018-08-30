#pragma once

#include "Common.h"
#include <libdevcore/easylog.h>
#include <libdevcore/FileSystem.h>
#include <boost/filesystem.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl.hpp>

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

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SocketType;
typedef std::shared_ptr<SocketType> SocketTypePtr;

class RLPXSocket: public std::enable_shared_from_this<RLPXSocket>
{
public:
	RLPXSocket(ba::io_service& _ioService, boost::asio::ssl::context &sslContext)
	{
		//m_socket = std::make_shared<bi::tcp::socket>(_ioService);
		m_socket = std::make_shared<SocketType>(_ioService, sslContext);
	}
	~RLPXSocket() 
	{ 
		close(); 
	}
	
	bool isConnected() const 
	{
		//return m_socket->is_open();
		return m_socket->lowest_layer().is_open();
	}
	void close() 
	{ 
		try 
		{ 
			boost::system::error_code ec;
			m_socket->lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
			if (m_socket->lowest_layer().is_open())
				m_socket->lowest_layer().close();
		} 
		catch (...){} 
	}
	bi::tcp::endpoint remoteEndpoint() 
	{ 
		boost::system::error_code ec;

		return m_socket->lowest_layer().remote_endpoint(ec);
	}

	SocketType& ref()
	{
		return *m_socket;
	}

protected:
	SocketTypePtr m_socket;
};

}
}
