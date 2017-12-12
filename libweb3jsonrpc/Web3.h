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
 * @file: Web3.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include "Web3Face.h"

namespace dev
{
namespace rpc
{

class Web3: public Web3Face
{
public:
	Web3(std::string _clientVersion = "C++ (ethereum-cpp)"): m_clientVersion(_clientVersion) {}
	virtual RPCModules implementedModules() const override
	{
		return RPCModules{RPCModule{"web3", "1.0"}};
	}
	virtual std::string web3_sha3(std::string const& _param1) override;
	virtual std::string web3_clientVersion() override { return m_clientVersion; }
	
private:
	std::string m_clientVersion;
};

}
}
