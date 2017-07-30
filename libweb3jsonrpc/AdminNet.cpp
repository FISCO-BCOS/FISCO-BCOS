#include <jsonrpccpp/common/exception.h>
#include <libwebthree/WebThree.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/Common.h>
#include <libethereum/NodeConnParamsManagerApi.h>
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
	for (p2p::PeerSessionInfo const& peer: m_network.peers())
		ret.append(toJson(peer));
	return ret;
}

bool AdminNet::admin_addPeer(string const& _node)
{
	m_network.addPeer(p2p::NodeSpec(_node), p2p::PeerType::Required);
	return true;
}

//增加新的节点信息
bool AdminNet::admin_addNodePubKeyInfo(string const& _node)
{
	bool bRet = false;
	std::cout << "AdminNet::admin_addNodePubKeyInfo |" << _node << std::endl;
	NodeConnParams nodeParam(_node);
	if (!nodeParam.Valid())
	{
		std::cout << "AdminNet::admin_addNodePubKeyInfo  reserialize error : " << _node << std::endl;
		return bRet;
	}
	bRet = NodeConnManagerSingleton::GetInstance().addNewNodeConnInfo(nodeParam);
	if (!bRet)
	{
		std::cout << "admin_addNodePubKeyInfo node existed." << std::endl;
	}

	//发起广播  增加新的协议
	vector<NodeConnParams> vParams;
	vParams.push_back(nodeParam);
	NodeConnManagerSingleton::GetInstance().sendNodeInfoSync(vParams);

	std::cout << "admin_addNodePubKeyInfo sendNodeInfoSync.node id is " << nodeParam._sNodeId  << std::endl;
	
	//不进行连接，连接以合约中的为主
	//进行连接 需要序列化出他的enode信息
//	NodeConnManagerSingleton::GetInstance().connNode(nodeParam);
	return true;
}

//删除配置中的节点信息
//_node则为nodeid即可
bool AdminNet::admin_delNodePubKeyInfo(string const& _node)
{
	bool bExisted = false;
	std::cout << "AdminNet::admin_addNodePubKeyInfo |" << _node << std::endl;
	

	NodeConnManagerSingleton::GetInstance().delNodeConnInfo(_node, bExisted);
	if (bExisted)
	{	
		//发起广播  增加新的协议
		NodeConnManagerSingleton::GetInstance().sendDelNodeInfoSync(_node);

		//断掉连接以合约为主，这里主要做配置的同步落地
		////需要断掉连接
		//NodeConnManagerSingleton::GetInstance().disconnNode(_node);
		//std::cout << "delNodeconninfo node exiteds. node id is : " << _node << endl;
	}
	else
	{
		std::cout << "delNodeconninfo node not exiteds. node id is : " << _node << endl;
	}

	
	return true;
}

//展示所有的节点信息
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

//展示所有的节点信息
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