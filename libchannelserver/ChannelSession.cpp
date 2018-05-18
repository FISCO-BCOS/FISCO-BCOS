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
/**
 * @file: ChannelSession.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <iostream>
#include <libdevcore/easylog.h>

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include "ChannelException.h"
#include "ChannelSession.h"

using namespace dev::channel;
ChannelSession::ChannelSession() {
	_topics = std::make_shared<std::set<std::string> >();
}

Message::Ptr ChannelSession::sendMessage(Message::Ptr request, size_t timeout) throw(dev::channel::ChannelException) {
	try {
		class SessionCallback: public std::enable_shared_from_this<SessionCallback> {
		public:
			typedef std::shared_ptr<SessionCallback> Ptr;

			SessionCallback() {
				_mutex.lock();
			}

			void onResponse(dev::channel::ChannelException error, Message::Ptr message) {
				_error = error;
				_response = message;

				_mutex.unlock();
			}

			dev::channel::ChannelException _error;
			Message::Ptr _response;
			std::mutex _mutex;
		};

		SessionCallback::Ptr callback = std::make_shared<SessionCallback>();

		std::function<void(dev::channel::ChannelException, Message::Ptr)> fp = std::bind(&SessionCallback::onResponse, callback, std::placeholders::_1, std::placeholders::_2);
		asyncSendMessage(request, fp, timeout);

		callback->_mutex.lock();
		callback->_mutex.unlock();

		if (callback->_error.errorCode() != 0) {
			LOG(ERROR) << "asyncSendMessage错误:" << callback->_error.errorCode() << " " << callback->_error.what();
			throw callback->_error;
		}

		return callback->_response;
	}
	catch (std::exception &e) {
		LOG(ERROR) << "错误:" << e.what();
	}

	return Message::Ptr();
}

void ChannelSession::asyncSendMessage(Message::Ptr request, std::function<void(dev::channel::ChannelException, Message::Ptr)> callback, uint32_t timeout) {
	try {
		if (!_actived) {
			if(callback) {
				callback(ChannelException(-3, "session已失效"), Message::Ptr());
			}

			return;
		}

		ResponseCallback::Ptr responseCallback = std::make_shared<ResponseCallback>();

		if (callback) {
			responseCallback->seq = request->seq();
			responseCallback->callback = callback;

			if (timeout > 0) {
				std::shared_ptr<boost::asio::deadline_timer> timeoutHandler =
				    std::make_shared<boost::asio::deadline_timer>(*_ioService,
				            boost::posix_time::milliseconds(timeout));

				timeoutHandler->async_wait(
				    boost::bind(&ChannelSession::onTimeout, shared_from_this(),
				                boost::asio::placeholders::error, request->seq()));

				responseCallback->timeoutHandler = timeoutHandler;
			}
			_responseCallbacks.insert(std::make_pair(request->seq(), responseCallback));
		}

		std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
		request->encode(*buffer);
		auto session = shared_from_this();

		writeBuffer(buffer);


	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::handshake(bool enableSSL, bool isServer) {
	if (enableSSL) {
		if (isServer) {
			_sslSocket->async_handshake(boost::asio::ssl::stream_base::server, boost::bind(&ChannelSession::onHandshake, shared_from_this(), boost::asio::placeholders::error));
		}
		else {
			_sslSocket->async_handshake(boost::asio::ssl::stream_base::client, boost::bind(&ChannelSession::onHandshake, shared_from_this(), boost::asio::placeholders::error));
		}
	}
}

void ChannelSession::run() {
	try {
		if (!_actived) {
			_actived = true;

			updateIdleTimer();

			startRead();
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::setSSLSocket(std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > socket) {
	_sslSocket = socket;

	_idleTimer = std::make_shared<boost::asio::deadline_timer>(_sslSocket->get_io_service());
	//_idleTimer->async_wait(boost::bind(&ChannelSession::onIdle, shared_from_this(), boost::asio::placeholders::error));
}

void ChannelSession::onHandshake(const boost::system::error_code& error) {
	try {
		if (!error) {
			startRead();
		}
		else {
			LOG(ERROR) << "SSL handshake error: " << error.message();

			try {
				_sslSocket->lowest_layer().close();
			}
			catch (std::exception &e) {
				LOG(ERROR) << "shutdown error:" << e.what();
			}
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::startRead() {
	try {
		if (_actived) {
			LOG(TRACE) << "start read:" << bufferLength;

			std::lock_guard<std::recursive_mutex> lock(_mutex);

			auto session = shared_from_this();
			_sslSocket->async_read_some(boost::asio::buffer(_recvBuffer, bufferLength),
			                            boost::bind(&ChannelSession::onRead, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}


void ChannelSession::onRead(const boost::system::error_code& error, size_t bytesTransferred) {
	try {
		updateIdleTimer();

		if (!error) {
			LOG(TRACE) << "read: " << bytesTransferred << " byte data:" << std::string(_recvBuffer, _recvBuffer + bytesTransferred);

			_recvProtocolBuffer.insert(_recvProtocolBuffer.end(), _recvBuffer, _recvBuffer + bytesTransferred);

			while (true) {
				auto message = std::make_shared<Message>();
				ssize_t result = message->decode(_recvProtocolBuffer.data(), _recvProtocolBuffer.size());
				LOG(TRACE) << "parse result: " << result;


				if (result > 0) {
					LOG(TRACE) << "parse result: " << result;

					onMessage(ChannelException(0, ""), message);

					_recvProtocolBuffer.erase(_recvProtocolBuffer.begin(), _recvProtocolBuffer.begin() + result);
				}
				else if (result == 0) {
					startRead();

					break;
				}
				else if (result < 0) {
					LOG(ERROR) << "parse error: " << result;

					disconnect(ChannelException(-1, "parse error, disconnect"));

					break;
				}
			}
		}
		else {
			LOG(WARNING) << "read error:" << error.value() << "," << error.message();

			if (_actived) {
				disconnect(ChannelException(-1, "readerror，disconnect "));
			}
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::startWrite() {
	if (_writing) {
		return;
	}

	if (!_sendBufferList.empty()) {
		_writing = true;

		auto buffer = _sendBufferList.front();
		_sendBufferList.pop();

		auto session = shared_from_this();
		_sslSocket->get_io_service().post(
		[ = ] {
			boost::asio::async_write(*_sslSocket,
			boost::asio::buffer(buffer->data(), buffer->size()),
			boost::bind(&ChannelSession::onWrite, session, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		});
	} else {
		_writing = false;
	}
}

void ChannelSession::writeBuffer(std::shared_ptr<bytes> buffer) {
	try {
		std::lock_guard<std::recursive_mutex> lock(_mutex);

		_sendBufferList.push(buffer);

		startWrite();
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::onWrite(const boost::system::error_code& error, size_t bytesTransferred) {
	try {
		std::lock_guard<std::recursive_mutex> lock(_mutex);

		updateIdleTimer();

		if (!error) {
			LOG(TRACE) << "write successed:" << bytesTransferred;
		}
		else {
			LOG(ERROR) << "write error: " << error.message();

			disconnect(ChannelException(-1, "write error，disconnect"));
		}

		_writing = false;
		startWrite();
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::onMessage(ChannelException e, Message::Ptr message) {
	try {
		auto it = _responseCallbacks.find(message->seq());
		if (it != _responseCallbacks.end()) {
			if(it->second->timeoutHandler.get() != NULL) {
			it->second->timeoutHandler->cancel();
			}

			if (it->second->callback) {
				_threadPool->enqueue([=]() {
				it->second->callback(e, message);
					_responseCallbacks.erase(it);
				});
			}
			else {
				LOG(ERROR) << "callback为空";

			_responseCallbacks.erase(it);
			}
		}
		else {
			if (_messageHandler) {
				_threadPool->enqueue([=]() {
				_messageHandler(ChannelException(0, ""), message);
				});
			}
			else {
				LOG(ERROR) << "messageHandler为空";
			}
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "错误:" << e.what();
	}
}

void ChannelSession::onTimeout(const boost::system::error_code& error, std::string seq) {
	try {
		auto it = _responseCallbacks.find(seq);
		if (it != _responseCallbacks.end()) {
			if (it->second->callback) {
				it->second->callback(ChannelException(-2, "回包超时"), Message::Ptr());
			}
			else {
				LOG(ERROR) << "callback empty";
			}

			_responseCallbacks.erase(it);
		} else {
			LOG(WARNING) << "seq timeout: " << seq << " callback successed？";
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error:" << e.what();
	}
}

void ChannelSession::onIdle(const boost::system::error_code& error) {
	try {
		if (error != boost::asio::error::operation_aborted) {
			disconnect(ChannelException(-1, "idle connection, disconnect"));
		}
		else {
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "error" << e.what();
	}
}

void ChannelSession::disconnect(dev::channel::ChannelException e) {
	try {
		std::lock_guard<std::recursive_mutex> lock(_mutex);
		if (_actived) {
			_idleTimer->cancel();
			if (!_responseCallbacks.empty()) {
				for (auto it : _responseCallbacks) {
					try {
						if (it.second->timeoutHandler.get() != NULL) {
							it.second->timeoutHandler->cancel();
						}

						if (it.second->callback) {
							_threadPool->enqueue([=]() {
								it.second->callback(e, Message::Ptr());

								//_responseCallbacks.erase(it);
								});
						} else {
							LOG(ERROR)<< "callback为空";

							//_responseCallbacks.erase(it);
						}
					}
					catch (std::exception &e) {
						LOG(ERROR) << "disconnect执行responseCallback: " << it.first << " 错误:" << e.what();
					}
				}
			}

			if (_messageHandler) {
				try {
					_messageHandler(e, Message::Ptr());
				}
				catch (std::exception &e) {
					LOG(ERROR) << "disconnect执行messageHandler错误 e:" << e.what();
				}
			}

			_actived = false;
			_sslSocket->lowest_layer().close();

			LOG(DEBUG) << "disconnected";
		}
	}
	catch (std::exception &e) {
		LOG(WARNING) << "close error:" << e.what();
	}
}

void ChannelSession::updateIdleTimer() {
	_idleTimer->expires_from_now(boost::posix_time::seconds(5));
	_idleTimer->async_wait(boost::bind(&ChannelSession::onIdle, shared_from_this(), boost::asio::placeholders::error));
}
