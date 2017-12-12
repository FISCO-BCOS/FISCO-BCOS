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
 * @file: AdminUtils.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <libwebthree/SystemManager.h>
#include "AdminUtilsFace.h"

namespace dev
{
namespace rpc
{

class SessionManager;

class AdminUtils: public dev::rpc::AdminUtilsFace
{
public:
	AdminUtils(SessionManager& _sm, SystemManager* _systemManager = nullptr);
	virtual RPCModules implementedModules() const override
	{
		return RPCModules{RPCModule{"admin", "1.0"}};
	}
	virtual bool admin_setVerbosity(int _v, std::string const& _session) override;
	virtual bool admin_verbosity(int _v) override;
	virtual bool admin_exit(std::string const& _session) override;

private:
	SessionManager& m_sm;
	SystemManager* m_systemManager = nullptr;
};

}
}
