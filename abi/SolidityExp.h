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
 * @file: SolidityExp.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __SOLIDITYEXP_H__
#define __SOLIDITYEXP_H__
#include <exception>
#include <string>

#define ABI_EXCEPTION_THROW(msg,error_code) do{ throw libabi::AbiException(msg,error_code,__FUNCTION__);} while (0);

namespace libabi
{
	//abi模块相关的错误的编码
	enum class EnumAbiExceptionErrCode
	{
		EnumAbiExceptionErrCodeUnkown                                = 901,  //未编码的错误
		EnumAbiExceptionErrCodeInvalidArgument                       = 101,  //调用的参数错误
		EnumAbiExceptionErrCodeContractNotExist                      = 102,  //调用合约不存在
		EnumAbiExceptionErrCodeFunctionNotExist                      = 103,  //调用合约的接口不存在
		EnumAbiExceptionErrCodeInvalidAbiType                        = 104,  //不合法的abi类型
		EnumAbiExceptionErrCodeInvalidAbiFormat                      = 105,  //不合法的abi,格式错误
		EnumAbiExceptionErrCodeInvalidAbiDecodeData                  = 106,  //不合法的abi decode数据
		EnumAbiExceptionErrCodeInvalidAbiAddrFromChainFailed         = 107,  //从链上获取abi合约地址失败
		EnumAbiExceptionErrCodeInvalidAbiCallInvokeFailed            = 108,  //call调用失败
		EnumAbiExceptionErrCodeInvalidAbiCallInvokeOnNotConstantFunc = 109,  //在非constant函数上调用call
		EnumAbiExceptionErrCodeInvalidAbiTransactionOnConstantFunc   = 110,  //在constant函数上发送交易
		EnumAbiExceptionErrCodeInvalidJsonFormat                     = 111,  //不合法的参数格式错误
		EnumAbiExceptionErrCodeAbiDBMgrAlreadyInit                   = 112,  //abi db管理已经被初始化
		EnumAbiExceptionErrCodeAbiDBMgrNotInit                       = 113,   //abi db管理没有初始化
		EnumAbiExceptionErrCodeAbiDBDATAChanged                      = 114   //leveldb的数据貌似被人修改 
	};

	//abi管理模块的异常
	class AbiException : public std::exception
	{
	public:
		AbiException(const std::string &msg, const EnumAbiExceptionErrCode &e = EnumAbiExceptionErrCode::EnumAbiExceptionErrCodeUnkown, const std::string &strFunc = "");
		//~AbiException() noexcept override;

		char const* what() const noexcept override;
		char const* what_func() const noexcept;
		EnumAbiExceptionErrCode error_code() const noexcept;

	protected:
		std::string msg_;
		EnumAbiExceptionErrCode e_;
		std::string func_; //异常发生的函数
	};
}//namespace libabi

#endif//__SOLIDITYEXP_H__
