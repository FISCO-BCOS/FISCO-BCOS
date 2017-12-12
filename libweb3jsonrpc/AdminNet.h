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
 * @file: AdminNet.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include "AdminNetFace.h"

namespace dev
{

class NetworkFace;

namespace rpc
{

class SessionManager;

class AdminNet: public dev::rpc::AdminNetFace
{
public:
	AdminNet(NetworkFace& _network, SessionManager& _sm);
	virtual RPCModules implementedModules() const override
	{
		return RPCModules{RPCModule{"admin", "1.0"}};
	}
	virtual bool admin_net_start(std::string const& _session) override;
	virtual bool admin_net_stop(std::string const& _session) override;
	virtual bool admin_net_connect(std::string const& _node, std::string const& _session) override;
	virtual Json::Value admin_net_peers(std::string const& _session) override;
	virtual Json::Value admin_net_nodeInfo(std::string const& _session) override;
	virtual Json::Value admin_nodeInfo() override;
	virtual Json::Value admin_peers() override;
	virtual bool admin_addPeer(std::string const& _node) override;
	virtual bool admin_addNodePubKeyInfo(string const& _node) override;
	virtual Json::Value admin_NodePubKeyInfos() override;
	virtual Json::Value admin_ConfNodePubKeyInfos() override;
	virtual bool admin_delNodePubKeyInfo(string const& _node) override;

private:
	NetworkFace& m_network;
	SessionManager& m_sm;
};
	
}
}
