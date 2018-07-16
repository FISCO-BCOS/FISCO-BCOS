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
 * @file: ChannelException.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <string>

namespace dev
{

namespace channel {

class ChannelException: public std::exception {
public:
	ChannelException() {};
	ChannelException(int errorCode, const std::string &msg): _errorCode(errorCode), _msg(msg) {};

	virtual int errorCode() { return _errorCode; };
	#ifdef __APPLE__
		virtual const char* what() const _NOEXCEPT override { return _msg.c_str(); };
	#else
		virtual const char* what() const _GLIBCXX_USE_NOEXCEPT override { return _msg.c_str(); };
	#endif
	bool operator!() const {
		return _errorCode == 0;
	}

private:
	int _errorCode = 0;
	std::string _msg = "";
};

}

}
