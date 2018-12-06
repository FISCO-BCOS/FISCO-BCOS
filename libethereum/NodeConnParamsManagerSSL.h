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
 * @file: NodeParamsManagerSSL.h
 * @author: toxotguo
 * 
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


class NodeConnParamsManagerSSL : public NodeConnParamsManagerApi
{
public:

	NodeConnParamsManagerSSL();
	virtual void getAllNodeConnInfo(int, std::map<std::string, NodeConnParams> & mNodeConnParams) const override;
	virtual void getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const override;

	virtual void getAllNode(int, std::map<std::string, NodeParams> & mNodeParams) const override;
	virtual void getAllConnect( std::map<std::string, NodeIPEndpoint> & mConnectParams) const override ;
	virtual void connNode(const ConnectParams &param) override;
	virtual void disconnNode(const std::string & sNodeId) override;
	virtual void setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi) override;

	void callSysContractData(int const& blockNum);

	bool getPublicKey(u256 const& _idx, Public & _pub) const override;
	bool getIdx(p2p::NodeID const& _nodeId, u256 & _idx) const override;
	unsigned getMinerNum() const override;
	bool getAccountType(p2p::NodeID const& _nodeId, unsigned & _type) const override;
	unsigned getNodeNum() const override;

	void caModifyCallback(const std::string& pub256)override;
	void SetHost(std::shared_ptr<p2p::HostApi>) override;
	bool checkCertOut(const std::string& serialNumber)override;
private:
	bool checkNodesValid(const std::vector< NodeParams> &vNodes);
	bool diffNodes(const std::vector< NodeParams> &vNodeParams, std::vector< NodeParams> &vAddNodes, std::vector< NodeParams> &vDelNodes);
	bool nodeInfoHash(dev::h256& h);
	void updateBootstrapnodes()const;

	//miner node
	static Mutex m_mutexminer;
	mutable std::map<std::string, NodeParams>	m_minernodes;

	NodeParams	m_self;
	static Mutex m_mutexconnect;
	mutable std::map<std::string, NodeIPEndpoint> m_connectnodes;
	std::pair<bool, std::map<std::string, NodeParams> > getGodMiner(int  blockNum)const;
	std::shared_ptr<p2p::HostApi> m_host;
	bool m_godminer=false;
};
}
}

