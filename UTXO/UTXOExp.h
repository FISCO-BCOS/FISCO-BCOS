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
 * @file: UTXOExp.h
 * @author: chaychen
 * 
 * @date: 2018
 */

#ifndef __UTXOEXP_H__
#define __UTXOEXP_H__

#include <exception>
#include <string>

#define UTXO_EXCEPTION_THROW(msg,error_code) do{ throw UTXOModel::UTXOException(msg,error_code,__FUNCTION__);} while (0);

namespace UTXOModel
{
	// UTXO模块相关的错误的编码
	enum class EnumUTXOExceptionErrCode
	{
		EnumUTXOExceptionErrCodeUnkown									= 100,	// Uncoded error
		EnumUTXOExceptionErrCodeTokenIDInvalid							= 101,	// Token ID is invalid.
		EnumUTXOExceptionErrCodeTxIDInvalid								= 102,	// Tx ID is invalid.
		EnumUTXOExceptionErrCodeAccountInvalid							= 103,	// Account is invalid.
		EnumUTXOExceptionErrCodeTokenUsed								= 104,	// Token has been used.
		EnumUTXOExceptionErrCodeTokenOwnerShipCheckFail					= 105,	// The ownership validation of token does not pass through.
		EnumUTXOExceptionErrCodeTokenLogicCheckFail						= 106,	// The logical validation of token does not pass through.
		EnumUTXOExceptionErrCodeTokenAccountingBalanceFail				= 107,	// The accounting equation verification of transaction does not pass through.
		EnumUTXOExceptionErrCodeAccountBalanceInsufficient				= 108,	// The balance of the account is insufficient.
		EnumUTXOExceptionErrCodeJsonParamError							= 109,	// Json parameter formatting error.
		EnumUTXOExceptionErrCodeUTXOTypeInvalid							= 110,	// UTXO transaction type error.
		EnumUTXOExceptionErrCodeAccountRegistered						= 111,	// Account has been registered.
		EnumUTXOExceptionErrTokenCntOutofRange							= 112,	// The number of Token numbers used in the transaction is beyond the limit(max=1000).
		EnumUTXOExceptionErrLowEthVersion								= 113,	// Please upgrade the environment for UTXO transaction.
		EnumUTXOExceptionErrCodeOtherFail								= 114	// Other Fail.
	};

	class UTXOException : public std::exception
	{
	public:
		UTXOException(const std::string &msg, const EnumUTXOExceptionErrCode &e = EnumUTXOExceptionErrCode::EnumUTXOExceptionErrCodeUnkown, const std::string &strFunc = "");
		//~UTXOException() noexcept override;

		char const* what() const noexcept override;
		char const* what_func() const noexcept;
		EnumUTXOExceptionErrCode error_code() const noexcept;

	protected:
		std::string msg_;
		EnumUTXOExceptionErrCode e_;
		std::string func_;
	};
}//namespace UTXOModel

#endif//__UTXOEXP_H__
