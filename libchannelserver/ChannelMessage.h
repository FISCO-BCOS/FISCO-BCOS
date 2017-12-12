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
#include <libdevcore/FixedHash.h>
#include <arpa/inet.h>

namespace dev
{

namespace channel {

struct Message {
	typedef std::shared_ptr<Message> Ptr;
	//包头组成，长度 + 类型 + seq(32字节UUID) + 结果
	const static size_t HEADER_LENGTH = 4 + 2 + 32 + 4;
	const static size_t MAX_LENGTH = 1024 * 1024 * 1024;

	uint32_t length = 0; //可判断http，走原http流程
	uint16_t type = 0;
	std::string seq = ""; //32字节
	int result = 0;

	std::shared_ptr<bytes> data;

	Message() {
		data = std::make_shared<bytes>();
	}

	void encode(bytes &buffer) {
		uint32_t lengthN = htonl(HEADER_LENGTH + data->size());
		uint16_t typeN = htons(type);
		//uint32_t seqN = htonl(seq);
		int32_t resultN = htonl(result);

		buffer.insert(buffer.end(), (byte*) &lengthN,	(byte*) &lengthN + sizeof(lengthN));
		buffer.insert(buffer.end(), (byte*) &typeN, (byte*) &typeN + sizeof(typeN));
		//buffer.insert(buffer.end(), (byte*) &seqN, (byte*) &seqN + sizeof(seqN));
		buffer.insert(buffer.end(), seq.data(), seq.data() + seq.size());
		buffer.insert(buffer.end(), (byte*) &resultN,	(byte*) &resultN + sizeof(resultN));

		buffer.insert(buffer.end(), data->begin(), data->end());
	}

	ssize_t decode(const byte* buffer, size_t size) {
		if(size < HEADER_LENGTH) {
			return 0;
		}

		length = ntohl(*((uint32_t*)&buffer[0]));

		if(length > MAX_LENGTH) {
			return -1;
		}

		if(size < length) {
			return 0;
		}

		type = ntohs(*((uint16_t*)&buffer[4]));
		seq = ntohl(*((uint32_t*)&buffer[6]));
		seq.assign(&buffer[6], &buffer[6] + 32);
		result = ntohl(*((uint32_t*)&buffer[38]));

		data->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + length - HEADER_LENGTH);

		return length;
	}
};

}

}
