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
 * @file: ChannelMessage.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <string>
#include <thread>
#include <queue>
#include <boost/lexical_cast.hpp>
#include <libdevcore/FixedHash.h>
#include <arpa/inet.h>
#include "http_parser.h"
#include "ChannelException.h"

namespace dev
{

namespace channel {

class Message: public std::enable_shared_from_this<Message> {
public:
	typedef std::shared_ptr<Message> Ptr;
	//包头组成，长度 + 类型 + seq(32字节UUID) + 结果

	Message() {
		_data = std::make_shared<bytes>();
	}

	virtual ~Message() {}
		//uint32_t seqN = htonl(seq);
	virtual void encode(bytes &buffer) {
		uint32_t lengthN = htonl(HEADER_LENGTH + _data->size());
		uint16_t typeN = htons(_type);
		int32_t resultN = htonl(_result);

		buffer.insert(buffer.end(), (byte*) &lengthN,	(byte*) &lengthN + sizeof(lengthN));
		buffer.insert(buffer.end(), (byte*) &typeN, (byte*) &typeN + sizeof(typeN));
		//buffer.insert(buffer.end(), (byte*) &seqN, (byte*) &seqN + sizeof(seqN));
		buffer.insert(buffer.end(), _seq.data(), _seq.data() + _seq.size());
		buffer.insert(buffer.end(), (byte*) &resultN,	(byte*) &resultN + sizeof(resultN));

		buffer.insert(buffer.end(), _data->begin(), _data->end());
	}

	virtual ssize_t decode(const byte* buffer, size_t size) {
		return decodeAMOP(buffer, size);
	}

	virtual bool isHttp(const byte* buffer, size_t size) {
		std::string cmd = std::string((const char*)buffer, size);

		if(cmd.find_first_of("GET ") == 0 || cmd.find_first_of("POST") == 0) {
			return true;
		}

		return false;
	}

	virtual void onHttpBody(const char* buffer, size_t size) {
		_data->assign((const byte*)buffer, (const byte*)buffer + size);
	}

	virtual ssize_t decodeHttp(const byte* buffer, size_t size) {
		//解析http协议
		std::shared_ptr<http_parser> httpParser = std::make_shared<http_parser>();
		http_parser_init(httpParser.get(), HTTP_REQUEST);

		std::shared_ptr<http_parser_settings>httpParserSettings = std::make_shared<http_parser_settings>();
		http_parser_settings_init(httpParserSettings.get());

		httpParserSettings->onBody = std::bind(&Message::onHttpBody, shared_from_this(), std::placeholders::_1, std::placeholders::_2);

		unsigned long count = http_parser_execute(httpParser.get(), httpParserSettings.get(), (const char*)buffer, size);

		if(httpParser->http_errno != 0) {
			throw(ChannelException(-1, "解析http请求错误:" + boost::lexical_cast<std::string>(httpParser->http_errno)));
		}

		if(!_data->empty()) {
			//解析完成
			//以太坊请求
			_length = _data->size();
			_type = 0x20;
			_seq = "";
			_result = 0;

			return _data->size();
		}

		return 0;
	}

	virtual ssize_t decodeAMOP(const byte* buffer, size_t size) {
		if(size < HEADER_LENGTH) {
			return 0;
		}

		_length = ntohl(*((uint32_t*) &buffer[0]));

		if (_length > MAX_LENGTH) {
			return -1;
		}

		if (size < _length) {
			return 0;
		}

		_type = ntohs(*((uint16_t*) &buffer[4]));
		_seq.assign(&buffer[6], &buffer[6] + 32);
		_result = ntohl(*((uint32_t*) &buffer[38]));

		_data->assign(&buffer[HEADER_LENGTH],
				&buffer[HEADER_LENGTH] + _length - HEADER_LENGTH);

		return _length;
	}

	virtual uint32_t length() { return _length; }

	virtual uint16_t type() { return _type; }
	virtual void setType(uint16_t type) { _type = type; }

	virtual std::string seq() { return _seq; }
	virtual void setSeq(std::string seq) { _seq = seq; }

	virtual int result() { return _result; }
	virtual void setResult(int result) { _result = result; }

	virtual byte* data() { return _data->data(); }
	virtual size_t dataSize() { return _data->size(); }

	virtual void setData(const byte *p, size_t size) { _data->assign(p, p + size); }

	virtual void clearData() { _data->clear(); }

protected:
	//最小包头，兼容http
	const static size_t MIN_HEADER_LENGTH = 4;

	//包头组成，长度 + 类型 + seq(32字节UUID) + 结果
	const static size_t HEADER_LENGTH = 4 + 2 + 32 + 4;
	const static size_t MAX_LENGTH = 1024 * 1024 * 1024; //最大支持1M的消息

	uint32_t _length = 0; //可判断http，走原http流程
	uint16_t _type = 0;
	std::string _seq = std::string(32, '0'); //32字节seq
	int _result = 0;

	std::shared_ptr<bytes> _data;
};

class TopicMessage: public Message {
public:
	typedef std::shared_ptr<TopicMessage> Ptr;

	TopicMessage() {};

	TopicMessage(Message *message) {
		_length = message->length();
		_type = message->type();
		_seq = message->seq();
		_result = message->result();

		_data = std::make_shared<bytes>(message->data(), message->data() + message->dataSize());
	}

	virtual ~TopicMessage() {}

	virtual std::string topic() {
		if(!(_type == 0x30 || _type == 0x31)) {
			throw(ChannelException(-1, "type: " + boost::lexical_cast<std::string>(_type) + " 非ChannelMessage"));
		}

		if(_data->size() < 1) {
			throw(ChannelException(-1, "错误，消息长度为0"));
		}

		uint8_t topicLen = *((uint8_t*)_data->data());

		if(_data->size() < topicLen) {
			throw(ChannelException(-1, "错误，topic长度不足 topicLen: " + boost::lexical_cast<std::string>(topicLen) + " size:" + boost::lexical_cast<std::string>(_data->size())));
		}

		std::string topic((char*)_data->data() + 1, topicLen - 1);

		return topic;
	}

	virtual void setTopic(const std::string &topic) {
		if(_data->size() > 0) {
			throw(ChannelException(-1, "禁止在已有data时设置topic"));
		}

		_data->push_back((char) topic.size() + 1);
		_data->insert(_data->end(), topic.begin(), topic.end());
	}

	virtual byte* data() override {
		std::string topic = this->topic();

		return _data->data() + 1 + topic.size();
	}

	virtual size_t dataSize() override {
		std::string topic = this->topic();

		return _data->size() - 1 - topic.size();
	}

	virtual void setData(const byte *p, size_t size) override {
		if(_data->empty()) {
			throw(ChannelException(-1, "禁止在无topic时设置data"));
		}

		_data->insert(_data->end(), p, p + size);
	}
};

}

}
