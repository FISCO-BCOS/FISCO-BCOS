/*
 * @CopyRight:
 * FISCO-BCOS-CHAIN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS-CHAIN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS-CHAIN.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 tianhe-dev contributors.
 */
/**
 * @file: ChannelConn.cpp
 * @author: tianheguoyun-alvin
 *
 * @date: 2018
 */

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

#include "ChannelConn.h"
#include "libchannelserver/ChannelException.h"
#include <libdevcore/Common.h>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>

using namespace dev::channel;

//ChannelConn::ChannelConn()

ChannelConn::~ChannelConn() 
{
// std::cout<<"~channelconn"<<std::endl;
 CHANNEL_LOG(TRACE) << "ChannelConn exit";
}

void ChannelConn::connect()
{

	if (_enableSSL)
	{
		//CHANNEL_LOG(TRACE) << LOG_DESC("Start SSL ChannelConn::connect");
		std::shared_ptr<std::string> sdkPublicKey = std::make_shared<std::string>();

        //_sslSocket->set_verify_mode(boost::asio::ssl::verify_peer);		 
		//_sslSocket->set_verify_callback(newVerifyCallback(sdkPublicKey));
        _sslSocket->set_verify_callback(boost::bind(&ChannelConn::verify_certificate, this, _1, _2));
	   
        //lowest_layer(),
 
		//boost::asio::ip::tcp::endpoint endpoint( boost::asio::ip::make_address(_host), _port);
        boost::asio::ip::tcp::resolver resolver(*_ioService);
        boost::asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(_host,std::to_string( _port)); 

	    boost::asio::async_connect(_sslSocket->lowest_layer(), endpoints,
             boost::bind(&ChannelConn::handle_connect, this,/*shared_from_this()*/ boost::asio::placeholders::error));	
        //_sslSocket->async_connect( endpoint , boost::bind(&ChannelConn::handle_connect, this;//shared_from_this(), boost::asio::placeholders::error));     
         
		 _actived = true;
		 updateIdleTimer();
	     _serverThread = std::make_shared<std::thread>([=]() {
	         pthread_setThreadName("ChannelServer");

	         try
	         {
			    _ioService->run();  // is this positorn ???
	         }
	         catch (std::exception& e)
	         {
	            CHANNEL_LOG(ERROR) << LOG_DESC("IO thread error")
	                                << LOG_KV("what", boost::diagnostic_information(e));
	         }

	     });		
		 
	}
	else
	{
		CHANNEL_LOG(TRACE) << LOG_DESC("Call connectionHandler");

		if (_connectionHandler)  // ???? is need
		{
			_connectionHandler(ChannelException(),shared_from_this());
		}
	}

}


bool ChannelConn::verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx)
{
    // The verify callback can be used to check whether the certificate that is
    // being presented is valid for the peer. For example, RFC 2818 describes
    // the steps involved in doing this for HTTPS. Consult the OpenSSL
    // documentation for more details. Note that the callback is called once
    // for each certificate in the certificate chain, starting from the root
    // certificate authority.

    // In this example we will simply print the certificate's subject name.
    
    char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    //std::cout << "verify_certificate  " << subject_name  <<" preverified  " << preverified << "\n";

    return preverified;
}

void ChannelConn::handle_connect(const boost::system::error_code& error)
 {
   if (!error)
   {
	    //std::cout << "handle_connect  handshakebegin call "  << "\n";
       
	   _sslSocket->async_handshake(boost::asio::ssl::stream_base::client,
	      boost::bind(&ChannelConn::handle_handshake, this,/*shared_from_this(),*/  boost::asio::placeholders::error));
   }
   else
   {
	 std::cout << "handle_connect failed: " << error.message() << "\n";
   }
 }
// handle_handshake  may be send heardstep
 void ChannelConn::handle_handshake(const boost::system::error_code& error)
 {
   if (!error)
   {
      //_sslSocket->async_handshake(boost::asio::ssl::stream_base::client,
      //   boost::bind(&ChannelConn::startWrite, this,  boost::asio::placeholders::error));
	 //std::cout << "Handshake call: " << error.message() << "\n";
     //;;_cliNetCV.notify_all(netreadlock);  
     _ready=true;
     _handshakeCV.notify_all(); 
     // then can write
   }
   else
   {
	 std::cout << "Handshake failed: " << error.message() << "\n";
   }
 }
 

void ChannelConn::setSSLSocket(
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket)
{
    _sslSocket = socket;
    //_idleTimer = std::make_shared<boost::asio::deadline_timer>(_sslSocket->get_io_service());
    _idleTimer = std::make_shared<boost::asio::deadline_timer>(*_ioService);
}

void ChannelConn::startRead()
{
    try
    {
        if (_actived)
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);

            auto session = this;//shared_from_this();
            if (_enableSSL)
            {
                _sslSocket->async_read_some(boost::asio::buffer(_recvBuffer, bufferLength),
                    [session](const boost::system::error_code& error, size_t bytesTransferred) {
                        auto s = session;
                        if (s)
                        {
                            s->onRead(error, bytesTransferred);
                        }
                    });
            }
            else
            {
                _sslSocket->next_layer().async_read_some(
                    boost::asio::buffer(_recvBuffer, bufferLength),
                    [session](const boost::system::error_code& error, size_t bytesTransferred) {
                        auto s = session;
                        if (s)
                        {
                            s->onRead(error, bytesTransferred);
                        }
                    });
            }
        }
        else
        {
            CHANNEL_SESSION_LOG(ERROR) << "startRead ChannelConn inactived";
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "startRead error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}


void ChannelConn::onRead(const boost::system::error_code& error, size_t bytesTransferred)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "onRead ChannelConn inactived";

            return;
        }

        updateIdleTimer();
        //std::cout<<"ChannelConn::onRead "<< bytesTransferred <<std::endl;
        if (!error)
        {
            _recvProtocolBuffer.insert(
                _recvProtocolBuffer.end(), _recvBuffer, _recvBuffer + bytesTransferred);

            while (true)
            {
                auto message = _messageFactory->buildMessage();

                ssize_t result =
                    message->decode(_recvProtocolBuffer.data(), _recvProtocolBuffer.size());

                if (result > 0)
                {
                    onMessage(ChannelException(0, ""), message);

                    _recvProtocolBuffer.erase(
                        _recvProtocolBuffer.begin(), _recvProtocolBuffer.begin() + result);
                }
                else if (result == 0)
                {
                    startRead();

                    break;
                }
                else if (result < 0)
                {
                    CHANNEL_SESSION_LOG(ERROR) << "onRead Protocol parser error";

                    disconnect(ChannelException(-1, "Protocol parser error, disconnect"));

                    break;
                }
            }
        }
        else
        {
            CHANNEL_SESSION_LOG(WARNING)
                << LOG_DESC("onRead failed") << LOG_KV("value", error.value())
                << LOG_KV("message", error.message());

            //if (_actived)
            {
                disconnect(ChannelException(-1, "Read failed, disconnect"));
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onRead error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelConn::startWrite()
{
    if (!_actived)
    {
        CHANNEL_SESSION_LOG(ERROR) << "startWrite ChannelConn inactived";

        return;
    }

    if (_writing)
    {
        return;
    }
    //std::cout<<"ChannelConn::startWrite call " <<std::endl;

    if (!_sendBufferList.empty())
    {
        _writing = true;

        auto buffer = _sendBufferList.front();
        _sendBufferList.pop();

        //auto session = std::weak_ptr<ChannelConn>(this;//shared_from_this());
        auto this_ptr = this; 
        _sslSocket->get_io_context().post([this_ptr, buffer] {
            auto s = this_ptr ; // session.lock();
            if (s)
            {
                if (s->enableSSL())
                {
                    boost::asio::async_write(*s->sslSocket(),
                        boost::asio::buffer(buffer->data(), buffer->size()),
                        [=](const boost::system::error_code& error, size_t bytesTransferred) {
                            //auto s = session.lock();
                            if (s)
                            {
                                s->onWrite(error, buffer, bytesTransferred);
                            }
                        });
                }
                else
                {
                    boost::asio::async_write(s->sslSocket()->next_layer(),
                        boost::asio::buffer(buffer->data(), buffer->size()),
                        [=](const boost::system::error_code& error, size_t bytesTransferred) {
                            //auto s = session.lock();
                            if (s)
                            {
                                s->onWrite(error, buffer, bytesTransferred);
                            }
                        });
                }
            }
        });
    }
    else
    {
        _writing = false;
    }
}

void ChannelConn::writeBuffer(std::shared_ptr<bytes> buffer)
{
    try
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);

        _sendBufferList.push(buffer);

        startWrite();
        startRead();
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "writeBuffer error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelConn::onWrite(const boost::system::error_code& error, std::shared_ptr<bytes>, size_t)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "onWrite ChannelConn inactived";

            return;
        }

        std::lock_guard<std::recursive_mutex> lock(_mutex);

        updateIdleTimer();

        if (error)
        {
            CHANNEL_SESSION_LOG(ERROR)
                << LOG_DESC("Write error") << LOG_KV("message", error.message());

            disconnect(ChannelException(-1, "Write error, disconnect"));
        }
        //std::cout<<" onWrite  call finished begin next startWrite" << std::endl;
        _writing = false;
        startWrite();
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << LOG_DESC("onWrite error") << LOG_KV("what", boost::diagnostic_information(e));
    }
}

// is need wait handshake finished 
void ChannelConn::asyncSendMessage(Message::Ptr request,
    std::function<void(dev::channel::ChannelException, Message::Ptr)> callback, uint32_t timeout)
{
    try
    {
        std::unique_lock<std::mutex> writelock( _handkshakeMutex);
        while(!_ready)        
        { 
           _handshakeCV.wait(writelock); 
        }
       
        if (!_actived)
        {
            if (callback)
            {
                callback(ChannelException(-3, "Session inactived"), Message::Ptr());
            }

            return;
        }

        std::shared_ptr<bytes> p_buffer = std::make_shared<bytes>();
       
        request->encode(*p_buffer);

        auto message = _messageFactory->buildMessage();

        ssize_t result = message->decode(p_buffer->data(), p_buffer->size());
        ssize_t result2 = request->decode(p_buffer->data(), p_buffer->size());

        //std::cout<< " ChannelConn::asyncSendMessage"<< "lengeh"<<p_buffer->size() << "result:"<< result << " res2: " << result << std::endl;

        writeBuffer(p_buffer);
		
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR) << LOG_DESC("asyncSendMessage error")
                                   << LOG_KV("what", boost::diagnostic_information(e));
    }
}

// 从这个消息里面取返回信息
void ChannelConn::onMessage(ChannelException e, Message::Ptr message)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << LOG_DESC("onMessage ChannelConn inactived");

            return;
        }

        {
            if (_messageHandler)
            {

                _messageHandler(shared_from_this(), ChannelException(0, ""), message);
            }
            else
            {
                CHANNEL_SESSION_LOG(ERROR) << "MessageHandler empty";
            }
        }

    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onMessage error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelConn::onIdle(const boost::system::error_code& error)
{
    try
    {
        if (!_actived)
        {
            CHANNEL_SESSION_LOG(ERROR) << "onIdle ChannelConn inactived";

            return;
        }

        if (error != boost::asio::error::operation_aborted)
        {
            CHANNEL_SESSION_LOG(ERROR) << "Idle connection, disconnect " << _host << ":" << _port;
            
            if(!_ready)
            {
                _handshakeCV.notify_all(); 
            }

            auto message = _messageFactory->buildMessage(); 
            _messageHandler(shared_from_this(), ChannelException(100, ""), message);

            disconnect(ChannelException(-1, "Idle connection, disconnect"));
        }
        else
        {
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(ERROR)
            << "onIdle error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelConn::simpleclose()
{
   
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _idleTimer->cancel();


    if (_sslSocket->next_layer().is_open())
    {
         LOG(WARNING) << "simple close  "<<_actived ;
         //_sslSocket->next_layer().shutdown(boost::asio::socket_base::shutdown_both);
         _sslSocket->next_layer().close();
         usleep(10); // is short ,will ~ChannelConn  no call
         _actived = false;
    }
}


void ChannelConn::disconnect(dev::channel::ChannelException e)
{
    try
    {
        LOG(WARNING) << "disconnect  close"<< _actived ;  
        if (_actived)
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _idleTimer->cancel();
            _actived = false;

            auto sslSocket = _sslSocket;
            // force close socket after 30 seconds
            auto shutdownTimer = std::make_shared<boost::asio::deadline_timer>(
                *_ioService, boost::posix_time::milliseconds(3));
            shutdownTimer->async_wait([sslSocket](const boost::system::error_code& error) {
                if (error && error != boost::asio::error::operation_aborted)
                {
                    LOG(WARNING) << "channel shutdown timer error"
                                 << LOG_KV("message", error.message());
                    return;
                }

                if (sslSocket->next_layer().is_open())
                {
                    LOG(WARNING) << "channel shutdown timeout force close";
                    sslSocket->next_layer().close();
                }
            });
			//auto  this_ptr = this;//shared_from_this(); /// modify by alvin
            _sslSocket->async_shutdown(
                [sslSocket, shutdownTimer](const boost::system::error_code& error) {
                    if (error)
                    {
                        LOG(WARNING) << "async_shutdown" << LOG_KV("message", error.message());
                    }
                    shutdownTimer->cancel();

                    if (sslSocket->next_layer().is_open())
                    {
                        sslSocket->next_layer().close();
                    }
                    
					//this_ptr->stop(); // disconnect and stop io_service , then  thread will stop .
                });

            CHANNEL_SESSION_LOG(DEBUG) << "Disconnected";
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_SESSION_LOG(WARNING)
            << "Close error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelConn::updateIdleTimer()
{
    if (!_actived)
    {
        CHANNEL_SESSION_LOG(ERROR) << "updateidletime ChannelConn inactived";

        return;
    }

    if (_idleTimer)
    {
        _idleTimer->expires_from_now(boost::posix_time::seconds(_idleTime));

        //auto this_ptr  = std::weak_ptr<ChannelConn>(this;//shared_from_this());
        auto this_ptr = this;
        _idleTimer->async_wait([this_ptr](const boost::system::error_code& error) {
            auto s = this_ptr;//.lock();
            if (s)
            {
                s->onIdle(error);
            }
        });
    }
}

// server connect response, server run this function
void dev::channel::ChannelConn::onHandshake(const boost::system::error_code& error,
    std::shared_ptr<std::string> _sdkPublicKey, ChannelConn::Ptr session)
{
    try
    {
        if (!error)
        {
            CHANNEL_LOG(TRACE) << LOG_DESC("SSL handshake success");
            if (_connectionHandler) // server deal mode
            {
                _connectionHandler(ChannelException(), session);
            }
            else
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("connectionHandler empty");
            }
            setRemotePublicKey(dev::h512(*_sdkPublicKey));
        }
        else
        {
            CHANNEL_LOG(WARNING) << LOG_DESC("SSL handshake error")
                                 << LOG_KV("message", error.message());

            try
            {
                sslSocket()->lowest_layer().close();
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(WARNING)
                    << "Close error" << LOG_KV("what", boost::diagnostic_information(e));
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_KV("what", boost::diagnostic_information(e));
    }
}


std::function<bool(bool, boost::asio::ssl::verify_context&)> 
dev::channel::ChannelConn::newVerifyCallback(std::shared_ptr<std::string> _sdkPublicKey)
{
    std::cout<< "i ChannelConn::newVerifyCallback"<<std::endl;
	auto clientptr  = this;//shared_from_this();
	return [clientptr, _sdkPublicKey](bool preverified, boost::asio::ssl::verify_context& ctx) {
		try
		{
			/// return early when the certificate is invalid
			if (!preverified)
			{
                std::cout<< " ChannelConn::newVerifyCallback preverified "<< preverified <<std::endl;
				return false;
			}
			/// get the object points to certificate
            std::cout<< " get the object points to certificate " <<std::endl;
			X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
			if (!cert)
			{
				CHANNEL_LOG(ERROR) << LOG_DESC("Get cert failed");
				return preverified;
			}

			int crit = 0;
			BASIC_CONSTRAINTS* basic =
				(BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
			if (!basic)
			{
				CHANNEL_LOG(ERROR) << LOG_DESC("Get ca basic failed");
				return preverified;
			}
			/// ignore ca
			if (basic->ca)
			{
				// ca or agency certificate
				CHANNEL_LOG(TRACE) << LOG_DESC("Ignore CA certificate");
				BASIC_CONSTRAINTS_free(basic);
				return preverified;
			}

			BASIC_CONSTRAINTS_free(basic);

			/// get issuer name
			if (clientptr->_checkCertIssuer)
			{
				const char* issuerName = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
				std::string issuer(issuerName);
				OPENSSL_free((void*)issuerName);

				if (issuer != clientptr->_certIssuerName)
				{
					CHANNEL_LOG(ERROR)
						<< LOG_DESC("The issuer of the two certificates are inconsistent.")
						<< LOG_KV("sdk certificate issuer", issuer)
						<< LOG_KV("node certificate issuer", clientptr->_certIssuerName);
					return false;
				}
			}

			getPublicKeyFromCert(_sdkPublicKey, cert);

			return preverified;
		}
		catch (std::exception& e)
		{
			CHANNEL_LOG(ERROR) << LOG_DESC("Cert verify failed")
							   << boost::diagnostic_information(e);
			return preverified;
		}
	};
}

bool dev::channel::getPublicKeyFromCert(std::shared_ptr<std::string> _nodeIDOut, X509* cert)
{
    EVP_PKEY* evpPublicKey = X509_get_pubkey(cert);
    if (!evpPublicKey)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("Get evpPublicKey failed");
        return false;
    }

    ec_key_st* ecPublicKey = EVP_PKEY_get1_EC_KEY(evpPublicKey);
    if (!ecPublicKey)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("Get ecPublicKey failed");
        return false;
    }
    /// get public key of the certificate
    const EC_POINT* ecPoint = EC_KEY_get0_public_key(ecPublicKey);
    if (!ecPoint)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("Get ecPoint failed");
        return false;
    }

    std::shared_ptr<char> hex =
        std::shared_ptr<char>(EC_POINT_point2hex(EC_KEY_get0_group(ecPublicKey), ecPoint,
                                  EC_KEY_get_conv_form(ecPublicKey), NULL),
            [](char* p) { OPENSSL_free(p); });

    if (hex)
    {
        _nodeIDOut->assign(hex.get());
        if (_nodeIDOut->find("04") == 0)
        {
            /// remove 04
            _nodeIDOut->erase(0, 2);
        }
    }
    return true;
}


void dev::channel::ChannelConn::stop()
{
	try
	{
		//CHANNEL_LOG(DEBUG) << LOG_DESC("stop() Close ioService");
        //disconnect(ChannelException(-1, "stop close , disconnect"));
        //usleep(100);
        simpleclose();
		_ioService->stop();
	}
	catch (std::exception& e)
	{
		CHANNEL_LOG(ERROR) << LOG_DESC("Close ioService")
						   << LOG_KV("what", boost::diagnostic_information(e));
	}
	_serverThread->join(); // ?? do what ,is need another thread call ?
}



