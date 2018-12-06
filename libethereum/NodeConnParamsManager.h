/*
	This file is part of FISCO-BCOS.

	FISCO-BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO-BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: NodeConnParamsManager.h
 * @author: toxotguo
 * @date: 2018
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

class NodeConnParamsManager : public NodeConnParamsManagerApi
{
public:

	static bool CAVerify;

	NodeConnParamsManager(std::string const& _json);

	virtual bool getNodeConnInfo(std::string const& sNodeID, NodeConnParams &retNode) const override;
	virtual bool getNodeConnInfoBoth(std::string const& sNodeID, NodeConnParams &retNode) const override;
	virtual void getAllNodeConnInfo(int, std::map<std::string, NodeConnParams> & mNodeConnParams) const override;
	virtual void getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const override;
	virtual void getAllConfNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const override;
	virtual void getAllNodeConnInfoContractAndConf(std::map<std::string, NodeConnParams> & mNodeConnParams) const override;

	virtual bool addNewNodeConnInfo(const std::string &sNewNodeConnInfo) override;
	virtual bool addNewNodeConnInfo(const NodeConnParams &nodeParam) override;
	virtual void delNodeConnInfo(const std::string &sNodeId, bool &bExisted) override;
	virtual	void sendNodeInfoSync(const std::vector<NodeConnParams> &vParams) override;
	virtual	void sendDelNodeInfoSync(const std::string &sNodeId) override;

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

	bool CheckAll(const std::string& sNodeId, CABaseData &caBaseData) override ;
	void ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd) override ;
	void SaveCADataInSession(const std::string nodeId, CABaseData &caBaseData) override ;
	void CAInfoModifyCallback(const std::string& pub256);
	void CAVerifyModifyCallback();
	void SetHost(std::shared_ptr<p2p::HostApi> host)override;
	bool CheckConnectCert(const std::string& serialNumber,const std::string& ip)override;

private:
	bool checkNodesValid(const std::vector< NodeConnParams> &vNodes);
	bool diffNodes(const std::vector< NodeConnParams> &vNodeParams, std::vector< NodeConnParams> &vAddNodes, std::vector< NodeConnParams> &vDelNodes);
	bool nodeInfoHash(dev::h256& h);

	bool signNodeInfo(CABaseData & caBaseData);
	bool signCAInfo(std::string seedStr, CABaseData & caBaseData);
	bool checkNodeInfo(const std::string& remoteNodeID, const dev::h256& h);
	bool checkNodeInfo(const std::string& remoteNodeID, CABaseData& caBaseData);
	bool checkIP(std::string remoteNodeID, CaInfo &caInfo);
	bool checkCA(std::string remoteNodeID, CABaseData & caBaseData);

	std::pair<bool, std::map<std::string, NodeConnParams> > getGodMiner(int  blockNum)const;
	
	mutable Mutex _xNodeConnParam;
	mutable std::map<std::string, NodeConnParams>	_mNodeConnParams;
	mutable Mutex _xConfigNodeConnParam;
	mutable std::map<std::string, NodeConnParams> _mConfNodeConnParams;
	std::shared_ptr<p2p::HostApi> m_host;
	bool m_godminer=false;
};

}
}

