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
 * @file: SolidityExp.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "SolidityExp.h"

namespace libabi
{
	AbiException::AbiException(const std::string &msg, const EnumAbiExceptionErrCode &e, const std::string &func) :msg_(msg), e_(e), func_(func)
	{
	}

	EnumAbiExceptionErrCode AbiException::error_code() const noexcept
	{
		return e_;
	}

	char const* AbiException::what_func() const noexcept
	{
		return func_.c_str();
	}

	char const* AbiException::what() const noexcept
	{
		return msg_.c_str();
	}
}//namespace libabi