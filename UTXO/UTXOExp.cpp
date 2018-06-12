/*
	This file is part of FISCO BCOS.

	FISCO BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: UTXOExp.cpp
 * @author: chaychen
 * 
 * @date: 2018
 */

#include "UTXOExp.h"

namespace UTXOModel
{
	UTXOException::UTXOException(const std::string &msg, const EnumUTXOExceptionErrCode &e, const std::string &func) :msg_(msg), e_(e), func_(func)
	{
	}

	EnumUTXOExceptionErrCode UTXOException::error_code() const noexcept
	{
		return e_;
	}

	char const* UTXOException::what_func() const noexcept
	{
		return func_.c_str();
	}

	char const* UTXOException::what() const noexcept
	{
		return msg_.c_str();
	}
}//namespace libabi