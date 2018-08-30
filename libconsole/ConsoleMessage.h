/*
 * ConsoleMessage.h
 *
 *  Created on: 2018年8月14日
 *      Author: monan
 */

#pragma once

#include <libchannelserver/Message.h>
#include <libdevcore/Common.h>

namespace dev {

namespace console {

class ConsoleMessage: public dev::channel::Message {
public:
	typedef std::shared_ptr<ConsoleMessage> Ptr;

	ConsoleMessage(): Message() {}

	virtual ~ConsoleMessage() {};

	const static size_t MAX_LENGTH = 1024; //max 1024byte

	virtual void encode(bytes &buffer) {
		buffer.insert(buffer.end(), _data->begin(), _data->end());
		if(_data->empty() || *_data->rbegin() != '\n') {
			buffer.push_back('\n');
		}
	}

	virtual ssize_t decode(const byte* buffer, size_t size) {
		size_t split = 0;
		size_t i = 0;

		for(i=0; i < size; ++i) {
			if(i > MAX_LENGTH) {
				return -1;
			}

			if(buffer[i] == '\n') {
				split = i + 1;
				if(i > 1 && buffer[i-1] == '\r') {
					--i;
			 	}

				if(i > 0) {
					_data->assign(buffer, buffer + i);
				}

				_length = i;
				return split;
			}
		}

		return 0;
	}
};

class ConsoleMessageFactory: public dev::channel::MessageFactory {
public:
	virtual ~ConsoleMessageFactory() {}
	virtual dev::channel::Message::Ptr buildMessage() override {
		return std::make_shared<ConsoleMessage>();
	}
};

}

}
