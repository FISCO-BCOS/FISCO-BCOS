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
 * @file: NodeConnParamsManager.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <libdevcore/FileSystem.h>
#include "NodeConnParamsManagerApi.h"

namespace dev
{
class NetworkFace;

namespace eth
{


class EthereumHost;
class SystemContractApi;

enum class CaStatus {
	Invalid,    
	Ok       
};
struct CaInfo
{
	std::string  hash;  
	std::string pubkey;
	std::string orgname;  
	u256 notbefore;
	u256 notafter;
	CaStatus status;
	std::string white;
	std::string black;

	u256    blocknumber;

	std::string toString()const
	{
		std::ostringstream os;
		os << hash << "|" << pubkey << "|" << orgname << "|" << notbefore << "|" << notafter << "|" << (int)status << "|" << blocknumber;
		os << "|white:";
		std::vector<std::string> whitelist = getWhiteList();
		for (std::vector<std::string>::iterator it = whitelist.begin(); it != whitelist.end(); it++)
			os << (*it);
		os << "|black:";
		std::vector<std::string> blacklist = getBlackList();
		for (std::vector<std::string>::iterator it = blacklist.begin(); it != blacklist.end(); it++)
			os << (*it);

		return os.str();
	}

	std::vector<std::string> getWhiteList() const
	{
		std::vector<std::string> whitelist;
		boost::split(whitelist, white, boost::is_any_of(";"));
		return whitelist;
	}
	std::vector<std::string> getBlackList() const
	{
		std::vector<std::string> blacklist;
		boost::split(blacklist, black, boost::is_any_of(";"));
		return blacklist;
	}

};


class NodeConnParamsManager : public NodeConnParamsManagerApi
{
public:

	static bool CAVerify;

	NodeConnParamsManager(std::string const& _json);

	
	virtual bool addNewNodeConnInfo(const std::string &sNewNodeConnInfo) override;
	
	virtual bool addNewNodeConnInfo(const NodeConnParams &nodeParam) override;

	virtual void delNodeConnInfo(const std::string &sNodeId, bool &bExisted) override;

	
	virtual	void sendNodeInfoSync(const std::vector<NodeConnParams> &vParams) override;

	
	virtual	void sendDelNodeInfoSync(const std::string &sNodeId) override;

	
	virtual void getAllNodeConnInfo(int blockNum, std::map<std::string, NodeConnParams> & mNodeConnParams) const override;
	
	virtual void connNode(const NodeConnParams &param) override;

	
	virtual void disconnNode(const std::string & sNodeId) override;

	virtual void setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi) override;

	
	void writeNodeFile();

	void callSysContractData(int const& blockNum);

	bool getPublicKey(u256 const& _idx, Public & _pub) const override;
	bool getIdx(p2p::NodeID const& _nodeId, u256 & _idx) const override;
	unsigned getMinerNum() const override;
	bool getAccountType(p2p::NodeID const& _nodeId, unsigned & _type) const override;
	unsigned getNodeNum() const override;

	std::string _sConfig;

	/*
	 * CA
	 */
	// DEPRECATED
	bool CheckAndSerialize(const RLP &_rlp, RLPBaseData &_rbd, CABaseData &caBaseData) override;
	// DEPRECATED
	bool CheckAll(const std::string& sNodeId, CABaseData &caBaseData) override ;
	// DEPRECATED
	void ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd) override ;
	// DEPRECATED
	void SaveCADataInSession(const std::string nodeId, CABaseData &caBaseData) override ;
	void CAInfoModifyCallback(const std::string& pub256);
	void CAVerifyModifyCallback();
	void SetHost(Host *host);
	bool CheckConnectCert(const std::string& serialNumber,const std::string& ip);

private:
	bool checkNodesValid(const std::vector< NodeConnParams> &vNodes);
	bool diffNodes(const std::vector< NodeConnParams> &vNodeParams, std::vector< NodeConnParams> &vAddNodes, std::vector< NodeConnParams> &vDelNodes);
	bool nodeInfoHash(dev::h256& h);
	// DEPRECATED
	bool signNodeInfo(CABaseData & caBaseData);
	// DEPRECATED
	bool signCAInfo(std::string seedStr, CABaseData & caBaseData);
	bool checkNodeInfo(const std::string& remoteNodeID, const dev::h256& h);
	// DEPRECATED
	bool checkNodeInfo(const std::string& remoteNodeID, CABaseData& caBaseData);
	bool checkIP(std::string remoteNodeID, CaInfo &caInfo);
	// DEPRECATED
	bool checkCA(std::string remoteNodeID, CABaseData & caBaseData);


	std::pair<bool, std::map<std::string, NodeConnParams> > getGodMiner(int  blockNum)const;

	Host* m_host;

	bool m_godminer=false;
};

}
}

