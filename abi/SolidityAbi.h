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
 * @file: SolidityAbi.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#ifndef __SOLIDITYABI_H__
#define __SOLIDITYABI_H__
#include <string>
#include <vector>
#include <algorithm>
#include <json/json.h> //for cpp json
#include <libdevcore/RLP.h>

namespace libabi
{
	//abi
	class SolidityAbi
	{
	public:
		SolidityAbi() = default;
		//构造，复制，赋值，移动，析构函数
		SolidityAbi(const std::string &strContractName, const std::string &strVer, const std::string &strAddr, const std::string &strAbi, dev::u256 uBlockNumber, dev::u256 uTimeStamp);
		SolidityAbi(const dev::bytes &_rlp);
		SolidityAbi(const SolidityAbi &) = default;
		SolidityAbi(SolidityAbi &&) = default;
		SolidityAbi &operator=(const SolidityAbi &) = default;
		~SolidityAbi() = default;

		//rlp编解码
		void streamRLP(dev::RLPStream& _s) const;
		dev::bytes rlp() const { dev::RLPStream s; streamRLP(s); return s.out(); }

	public:
		//重置abi信息
		void reset(const std::string &strContractName, const std::string &strVer, const std::string &strAddr, const std::string &strAbi, dev::u256 uBlockNumber, dev::u256 uTimeStamp);

	public:
		//abi中的一个函数的描述信息
		struct Function
		{
			//函数中的一个input
			struct Input
			{
				std::string strName;
				std::string strType;
			};
			//函数中的一个output
			using Output = Input;

			std::string strName;
			std::string strType{ "function" }; //构造函数strType为constructor
			std::string sha3Signature;

			bool isConstant;
			bool isPayable ;

			std::vector<Input> allInputs;
			std::vector<Output> allOutputs;

			inline bool bConstant() const noexcept
			{
				return isConstant;
			}

			inline bool bPayable() const noexcept
			{
				return isPayable;
			}

			std::vector<std::string> getAllInputType() const noexcept
			{
				std::vector<std::string> types;
				std::for_each(allInputs.begin(), allInputs.end(), [&types](const Input &i) {
					types.push_back(i.strType);
				});
				return types;
			}

			std::vector<std::string> getAllOutputType() const noexcept
			{
				std::vector<std::string> types;
				std::for_each(allOutputs.begin(), allOutputs.end(), [&types](const Input &i) {
					types.push_back(i.strType);
				});
				return types;
			}

			inline void setSha3Signature(const std::string sha3Sig) noexcept 
			{
				sha3Signature = sha3Sig;
			}

			inline std::string getSha3Signature() const noexcept
			{
				return sha3Signature;
			}

			//获取函数签名
			std::string getSignature() const noexcept
			{
				std::string strTmp = strName + "(";

				std::for_each(allInputs.begin(), allInputs.end(), [&strTmp](const Input &i) {
					strTmp += i.strType;
					strTmp += ",";
				});

				if (',' == strTmp.back())
				{
					strTmp.back() = ')';
				}
				else
				{
					strTmp += ")";
				}

				return strTmp;
			}
		};

		//构造函数也是一个函数,不过函数类型不是function
		using Constructor = Function;

		//abi中的一个event的描述信息
		struct Event
		{
			std::string strName;
			std::string strType{ "event" };
			bool isAnonymous{ false };

			struct Input
			{
				bool isIndexed{ false };
				std::string strName;
				std::string strType;
			};

			std::vector<Input> allInputs;
		};

	public:
		//从函数的完整签名构建一个函数对象
		Function constructFromFuncSig(const std::string &strFuncSig) const;

	private:
		//编译生成abi的合约的名称，不包含扩展名称
		std::string m_strContractName;
		//编译生成abi的合约的版本号
		std::string m_strVersion;
		//地址
		std::string m_strContractAddr;
		//abi信息
		std::string m_strAbi;
		//abi信息打入的block number
		dev::u256   m_uBlockNumber;
		//abi信息打入块的时间戳
		dev::u256   m_uTimeStamp;

	public:
		std::string getContractName() const { return m_strContractName; }
		std::string getVersion()      const { return m_strVersion;      }
		std::string getAddr()         const { return m_strContractAddr; }
		std::string getAbi()          const { return m_strAbi; }
		dev::u256   getBlockNumber()  const { return m_uBlockNumber; }
		dev::u256   getTimeStamp()    const { return m_uTimeStamp; }

		//abi中所有的函数
		std::vector<Function> m_allFunc;
		//abi中所有的事件
		std::vector<Event>    m_allEvent;

		//清理所有的信息
		void clear();
		void addOFunction(const Function &f) { m_allFunc.push_back(f); }
		void addOEvent(const Event &e) { m_allEvent.push_back(e); }
		//从json abi中解析函数 事件  构造函数信息
		void parserFunction(const Json::Value &jValue, Function &func);
		void parserEvent(const Json::Value &jValue, Event &event);
		void parserConstruct(const Json::Value &jValue, Function &func);
		void parserEventInput(const Json::Value &jValue, std::vector<Event::Input> &inputs);
		void parserFunctionInput(const Json::Value &jValue, std::vector<Function::Input> &inputs);
		void parserFunctionOutput(const Json::Value &jValue, std::vector<Function::Input> &outputs);

		//检查是否是一个正常的abi的输入或者是输入类型，其实abi都是编译器生成的，理论上来说不会有什么错误，这里做简单的检查
		void invalidAbiInputOrOutput(const std::string & strType);

	public:
		//根据函数名称或者是完整的函数签名获取函数对象
		// strFunc可以是函数名称         比如： get
		//        也可以是函数的完整签名 比如： set(int,string,uint[])
		// 调用cns服务时一般传入调用函数的名称即可,函数有重载的情况下,需要传入完整的函数签名,才能够决定调用哪个函数
		const Function &getFunction(const std::string &strFunc) const;
		const Function &getFunctionByFuncName(const std::string &strFuncName) const;
		const Function &getFunctionBySha3Sig(const std::string &strSha3Sig) const;
		//根据函数名获取函数完整签名
		std::string getFunctionSignature(const std::string &strFuncName);
		std::vector<std::string> getFunctionInputTypes(const std::string &strFuncName);
		std::vector<std::string> getFunctionOutputTypes(const std::string &strFuncName);
	};//class SolidityAbi
}//namespace libabi

#endif//__SOLIDITYABI_H__
