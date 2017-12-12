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
 * @file: SessionManager.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <unordered_set>
#include <unordered_map>

#define RPC_ADMIN if (!m_sm.hasPrivilegeLevel(_session, Privilege::Admin)) throw jsonrpc::JsonRpcException("Invalid privileges");

namespace dev
{
namespace rpc
{

enum class Privilege
{
	Admin
};

}
}

namespace std
{
	template<> struct hash<dev::rpc::Privilege>
	{
		size_t operator()(dev::rpc::Privilege _value) const { return (size_t)_value; }
	};
}

namespace dev
{
namespace rpc
{

struct SessionPermissions
{
	std::unordered_set<Privilege> privileges;
};

class SessionManager
{
public:
	std::string newSession(SessionPermissions const& _p);
	void addSession(std::string const& _session, SessionPermissions const& _p);
	bool hasPrivilegeLevel(std::string const& _session, Privilege _l) const;

private:
	std::unordered_map<std::string, SessionPermissions> m_sessions;
};

}
}
