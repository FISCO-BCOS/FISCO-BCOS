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
#include <libdevcore/Common.h>

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
			LOG(ERROR) << "asyncSendMessage ERROR:" << callback->_error.errorCode() << " " << callback->_error.what();
			throw callback->_error;
		}

		return callback->_response;
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
	}

	return Message::Ptr();
}

void ChannelSession::asyncSendMessage(Message::Ptr request, std::function<void(dev::channel::ChannelException, Message::Ptr)> callback, uint32_t timeout) {
	try {
		if (!_actived) {
			if(callback) {
				callback(ChannelException(-3, "Session inactived"), Message::Ptr());
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

				auto session = std::weak_ptr<ChannelSession>(shared_from_this());
				timeoutHandler->async_wait(
				    boost::bind(&ChannelSession::onTimeout, shared_from_this(),
				                boost::asio::placeholders::error, request->seq()));

				responseCallback->timeoutHandler = timeoutHandler;
			}
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_responseCallbacks.insert(std::make_pair(request->seq(), responseCallback));
		}

		std::shared_ptr<bytes> buffer = std::make_shared<bytes>();

		request->encode(*buffer);

		writeBuffer(buffer);


	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
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
		LOG(ERROR) << "ERROR:" << e.what();
	}
}

void ChannelSession::setSSLSocket(std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > socket) {
	_sslSocket = socket;

	_idleTimer = std::make_shared<boost::asio::deadline_timer>(_sslSocket->get_io_service());
	//_idleTimer->async_wait(boost::bind(&ChannelSession::onIdle, shared_from_this(), boost::asio::placeholders::error));
}

void ChannelSession::startRead() {
	try {
		if (_actived) {
			LOG(TRACE) << "Start read:" << bufferLength;

			std::lock_guard<std::recursive_mutex> lock(_mutex);

			//auto session = std::weak_ptr<ChannelSession>(shared_from_this());
			auto session = shared_from_this();
			_sslSocket->async_read_some(boost::asio::buffer(_recvBuffer, bufferLength),
						                            [session](const boost::system::error_code& error, size_t bytesTransferred) {
				//auto s = session.lock();
				auto s = session;
				if(s) {
					s->onRead(error, bytesTransferred);
				}
			});

		}
		else {
			LOG(ERROR) << "ChannelSession inactived";
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
	}
}


void ChannelSession::onRead(const boost::system::error_code& error, size_t bytesTransferred) {
	try {
		if(!_actived) {
			LOG(ERROR) << "ChannelSession inactived";

			return;
		}
		updateIdleTimer();

		if (!error) {
			LOG(TRACE) << "Read: " << bytesTransferred << " bytes data:" << std::string(_recvBuffer, _recvBuffer + bytesTransferred);

			_recvProtocolBuffer.insert(_recvProtocolBuffer.end(), _recvBuffer, _recvBuffer + bytesTransferred);

			while (true) {
				auto message = std::make_shared<Message>();
				ssize_t result = message->decode(_recvProtocolBuffer.data(), _recvProtocolBuffer.size());
				LOG(TRACE) << "protocol parse done: " << result;

				if (result > 0) {
					LOG(TRACE) << "parse done: " << result;

					onMessage(ChannelException(0, ""), message);

					_recvProtocolBuffer.erase(_recvProtocolBuffer.begin(), _recvProtocolBuffer.begin() + result);
				}
				else if (result == 0) {
					startRead();

					break;
				}
				else if (result < 0) {
					LOG(ERROR) << "Protocol parser error: " << result;

					disconnect(ChannelException(-1, "Protocol parser error, disconnect"));
					break;
				}
			}
		}
		else {
			LOG(ERROR) << "Read failed:" << error.value() << "," << error.message();

			if (_actived) {
				disconnect(ChannelException(-1, "Read failed, disconnect"));
			}
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
	}
}

void ChannelSession::startWrite() {
	if(!_actived) {
		LOG(ERROR) << "ChannelSession inactived";

		return;
	}
	if (_writing) {
		return;
	}

	if (!_sendBufferList.empty()) {
		_writing = true;

		auto buffer = _sendBufferList.front();
		_sendBufferList.pop();

		auto session = shared_from_this();

		_sslSocket->get_io_service().post(
		[ session, buffer ] {
			auto s = session;
			if(s && s->actived()) {
				std::lock_guard<std::recursive_mutex> lock(s->_mutex);
				boost::asio::async_write(*s->sslSocket(),
						boost::asio::buffer(buffer->data(), buffer->size()),
						[session, buffer](const boost::system::error_code& error, size_t bytesTransferred) {
							auto s = session;
							if(s && s->actived()) {
								std::lock_guard<std::recursive_mutex> lock(s->_mutex);
								s->onWrite(error, buffer, bytesTransferred);
							}
				});
			}
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
		LOG(ERROR) << "ERROR:" << e.what();
	}
}

void ChannelSession::onWrite(const boost::system::error_code& error, std::shared_ptr<bytes> buffer, size_t bytesTransferred) {
	try {
		if(!_actived) {
			LOG(ERROR) << "ChannelSession inactived";

			return;
		}
		std::lock_guard<std::recursive_mutex> lock(_mutex);

		updateIdleTimer();

		if (!error) {
			LOG(TRACE) << "Write: " << bytesTransferred;
		}
		else {
			LOG(ERROR) << "Write error: " << error.message();

			disconnect(ChannelException(-1, "Write error, disconnect"));
		}

		_writing = false;
		startWrite();
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
	}
}

void ChannelSession::onMessage(ChannelException e, Message::Ptr message) {
	try {
		if(!_actived) {
			LOG(ERROR) << "ChannelSession inactived";

			return;
		}
		ResponseCallback::Ptr callback;

		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
		auto it = _responseCallbacks.find(message->seq());
		if (it != _responseCallbacks.end()) {
				callback = it->second;
			}
			}

		if (callback) {
			if(callback->timeoutHandler.get() != NULL) {
				callback->timeoutHandler->cancel();
			}

			if (callback->callback) {
				_threadPool->enqueue([=]() {
					callback->callback(e, message);

					std::lock_guard<std::recursive_mutex> lock(_mutex);
					auto ite =  _responseCallbacks.find(message->seq());
					if(ite != _responseCallbacks.end()) {
						_responseCallbacks.erase(ite);
					}
				});
			}
			else {
				LOG(ERROR) << "Callback empty";

				std::lock_guard<std::recursive_mutex> lock(_mutex);
				auto ite =  _responseCallbacks.find(message->seq());
				if(ite != _responseCallbacks.end()) {
					_responseCallbacks.erase(ite);
				}
			}
		}
		else {
			if (_messageHandler) {
				auto session = std::weak_ptr<dev::channel::ChannelSession>(shared_from_this());
				_threadPool->enqueue([session, message]() {
					auto s = session.lock();
					if(s && s->_messageHandler) {
						s->_messageHandler(s, ChannelException(0, ""), message);
					}
				});
			}
			else {
				LOG(ERROR) << "MessageHandler empty";
			}
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
	}
}

void ChannelSession::onTimeout(const boost::system::error_code& error, std::string seq) {
	try {
		if(!_actived) {
			LOG(ERROR) << "ChannelSession inactived";

			return;
		}
		auto it = _responseCallbacks.find(seq);
		if (it != _responseCallbacks.end()) {
			if (it->second->callback) {
				it->second->callback(ChannelException(-2, "Response timeout"), Message::Ptr());
			}
			else {
				LOG(ERROR) << "Callback empty";
			}

			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_responseCallbacks.erase(it);
		} else {
			LOG(WARNING) << "Seq timeout: " << seq;
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
	}
}

void ChannelSession::onIdle(const boost::system::error_code& error) {
	try {
		if(!_actived) {
			LOG(ERROR) << "ChannelSession inactived";

			return;
		}
		if (error != boost::asio::error::operation_aborted) {
			LOG(ERROR) << "Idle connection, disconnect " << _host << ":" << _port;

			disconnect(ChannelException(-1, "Idle connection, disconnect"));
		}
		else {
		}
	}
	catch (std::exception &e) {
		LOG(ERROR) << "ERROR:" << e.what();
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
							LOG(ERROR)<< "Callback empty";

							//_responseCallbacks.erase(it);
						}
					}
					catch (std::exception &e) {
						LOG(ERROR) << "Disconnect responseCallback: " << it.first << " error:" << e.what();
					}
				}

				_responseCallbacks.clear();
			}

			if (_messageHandler) {
				try {
					auto self = shared_from_this();
					auto messageHandler = _messageHandler;
					_threadPool->enqueue([messageHandler, self, e]() {
						messageHandler(self, e, Message::Ptr());
					});
				}
				catch (std::exception &e) {
					LOG(ERROR) << "disconnect messageHandler error:" << e.what();
				}
				//_messageHandler = std::function<void(ChannelSession::Ptr, dev::channel::ChannelException, Message::Ptr)>();
			}

			_actived = false;

			auto sslSocket = _sslSocket;
			_sslSocket->async_shutdown([sslSocket](const boost::system::error_code& error) {
				if(error) {
					LOG(WARNING) << "Error while shutdown the ssl socket: " << error.message();
				}

				sslSocket->next_layer().close();
			});
			//_sslSocket->lowest_layer().close();

			LOG(DEBUG) << "Disconnected";
		}
	}
	catch (std::exception &e) {
		LOG(WARNING) << "Close error: " << e.what();
	}
}

void ChannelSession::updateIdleTimer() {
	if(!_actived) {
		LOG(ERROR) << "ChannelSession inactived";

		return;
	}

	if(_idleTimer) {
		_idleTimer->expires_from_now(boost::posix_time::seconds(5));
		auto session = std::weak_ptr<ChannelSession>(shared_from_this());
		_idleTimer->async_wait([session] (const boost::system::error_code& error){
			auto s = session.lock();
			if(s) {
				s->onIdle(error);
			}
		});
	}
}
