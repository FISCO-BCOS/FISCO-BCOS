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
 * @file: AdminNet.cpp
 * @author toxotguo
 * @date 2018
 */

#include <jsonrpccpp/common/exception.h>
#include <libwebthree/WebThree.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/easylog.h>
#include <libethcore/Common.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <libethereum/EthereumPeer.h>
#include <libethereum/EthereumHost.h>
#include "AdminNet.h"
#include "SessionManager.h"
#include "JsonHelper.h"

using namespace std;
using namespace dev;
using namespace dev::rpc;
using namespace dev::eth;

AdminNet::AdminNet(NetworkFace& _network, SessionManager& _sm): m_network(_network), m_sm(_sm) {}

bool AdminNet::admin_net_start(std::string const& _session)
{
	RPC_ADMIN;
	m_network.startNetwork();
	return true;
}

bool AdminNet::admin_net_stop(std::string const& _session)
{
	RPC_ADMIN;
	m_network.stopNetwork();
	return true;
}

bool AdminNet::admin_net_connect(std::string const& _node, std::string const& _session)
{
	RPC_ADMIN;
	return admin_addPeer(_node);
}

Json::Value AdminNet::admin_net_peers(std::string const& _session)
{
	RPC_ADMIN;
	return admin_peers();
}

Json::Value AdminNet::admin_net_nodeInfo(std::string const& _session)
{
	RPC_ADMIN;
	Json::Value ret;
	p2p::NodeInfo i = m_network.nodeInfo();
	ret["name"] = i.version;
	ret["port"] = i.port;
	ret["address"] = i.address;
	ret["listenAddr"] = i.address + ":" + toString(i.port);
	ret["id"] = i.id.hex();
	ret["enode"] = i.enode();
	return ret;
}

Json::Value AdminNet::admin_nodeInfo()
{
	Json::Value ret;
	p2p::NodeInfo i = m_network.nodeInfo();
	ret["name"] = i.version;
	ret["ports"] = Json::objectValue;
	// Both ports are equal as of 2016-02-04, migt change later
	ret["ports"]["discovery"] = i.port;
	ret["ports"]["listener"] = i.port;
	ret["ip"] = i.address;
	ret["listenAddr"] = i.address + ":" + toString(i.port);
	ret["id"] = i.id.hex();
	ret["enode"] = i.enode();
	ret["protocols"] = Json::objectValue;
	ret["protocols"]["eth"] = Json::objectValue; //@todo fill with information
	return ret;
}

Json::Value AdminNet::admin_peers()
{
	Json::Value ret;

	//get nodeId->blockNumber mapping
	std::map<h512, u256> nodeId_to_height;
	m_network.ethereum()->sharedHost()->getPeersHeight(nodeId_to_height);

	for (p2p::PeerSessionInfo const& peer : m_network.peers())
	{
		Json::Value peer_info_json = toJson(peer);
		peer_info_json["height"] = toJS(nodeId_to_height[peer.id]);
		ret.append(peer_info_json);
	}
	return ret;
}

bool AdminNet::admin_addPeer(string const& _node)
{
	m_network.addPeer(p2p::NodeSpec(_node), p2p::PeerType::Required);
	return true;
}

//add new peer info
bool AdminNet::admin_addNodePubKeyInfo(string const& _node)
{
	bool bRet = false;
	LOG(INFO) << "AdminNet::admin_addNodePubKeyInfo |" << _node << "\n";
	NodeConnParams nodeParam(_node);
	if (!nodeParam.Valid())
	{
		LOG(ERROR) << "AdminNet::admin_addNodePubKeyInfo  reserialize error : " << _node << "\n";
		return bRet;
	}
	bRet = NodeConnManagerSingleton::GetInstance().addNewNodeConnInfo(nodeParam);
	if (!bRet)
	{
		LOG(ERROR) << "admin_addNodePubKeyInfo node existed." << "\n";
	}

	//broadcast to add new protocal
	vector<NodeConnParams> vParams;
	vParams.push_back(nodeParam);
	NodeConnManagerSingleton::GetInstance().sendNodeInfoSync(vParams);

	LOG(INFO) << "admin_addNodePubKeyInfo sendNodeInfoSync.node id is " << nodeParam._sNodeId  << "\n";
	return true;
}

//delete peer info in config
bool AdminNet::admin_delNodePubKeyInfo(string const& _node)
{
	bool bExisted = false;
	LOG(INFO) << "AdminNet::admin_delNodePubKeyInfo |" << _node << "\n";


	NodeConnManagerSingleton::GetInstance().delNodeConnInfo(_node, bExisted);
	if (bExisted)
	{
		//broadcast to add new protocol
		NodeConnManagerSingleton::GetInstance().sendDelNodeInfoSync(_node);
	}
	else
	{
		LOG(ERROR) << "delNodeconninfo node not exiteds. node id is : " << _node << "\n";
	}


	return true;
}

//show all public keys info
Json::Value AdminNet::admin_NodePubKeyInfos()
{
	Json::Value ret;
	std::map<std::string, eth::NodeConnParams> mNodeConnParams;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfoContract(mNodeConnParams);

	for (auto const & param : mNodeConnParams)
	{
		ret.append(toJson(param.second));
	}

	return ret;
}

Json::Value AdminNet::admin_ConfNodePubKeyInfos()
{
	Json::Value ret;
	std::map<std::string, eth::NodeConnParams> mNodeConnParams;
	NodeConnManagerSingleton::GetInstance().getAllConfNodeConnInfo(mNodeConnParams);

	for (auto const & param : mNodeConnParams)
	{
		ret.append(toJson(param.second));
	}

	return ret;
}
