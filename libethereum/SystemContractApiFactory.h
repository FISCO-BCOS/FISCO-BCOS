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
 * @file: SystemContractApi.cpp
 * @author: fisco-dev
 * 
 * @date: 2018
 */

#include "SystemContractApi.h"
#include "Client.h"

namespace dev
{
namespace eth
{

class SystemContractApiFactory
{
    public:
		static std::shared_ptr<SystemContractApi> create(const Address _address, const Address _god, Client* _client);
};

}
}