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
 * @file: AdminUtils.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <jsonrpccpp/common/exception.h>
#include <libdevcore/easylog.h>
#include <libethereum/Client.h>
#include "SessionManager.h"
#include "AdminUtils.h"

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::rpc;

AdminUtils::AdminUtils(SessionManager& _sm, SystemManager* _systemManager):
	m_sm(_sm),
	m_systemManager(_systemManager)
{}

bool AdminUtils::admin_setVerbosity(int _v, std::string const& _session)
{
	RPC_ADMIN;
	return admin_verbosity(_v);
}

bool AdminUtils::admin_verbosity(int /*_v*/)
{
	//g_logVerbosity = _v;
	return true;
}

bool AdminUtils::admin_exit(std::string const& _session)
{
	RPC_ADMIN;
	if (m_systemManager)
	{
		m_systemManager->exit();
		return true;
	}
	return false;
}
