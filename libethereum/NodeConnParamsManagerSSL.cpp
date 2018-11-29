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
 * @file: NodeParamsManager.cpp
 * @author: toxotguo
 * 
 * @date: 2018
 */

#include <json_spirit/JsonSpiritHeaders.h>
#include <jsonrpccpp/common/specificationparser.h>
#include <libethcore/CommonJS.h>
#include <libp2p/Common.h>
#include <libweb3jsonrpc/JsonHelper.h>
#include <libwebthree/WebThree.h>
#include <libp2p/SessionWBCAData.h>
#include <libdevcore/easylog.h>
#include "EthereumHost.h"
#include "NodeConnParamsManagerSSL.h"
#include "SystemContractApi.h"

#include "libdevcore/Hash.h"
#include <fstream>
#include "libdevcrypto/Rsa.h"

using namespace std;
using namespace dev;
using namespace eth;
namespace js = json_spirit;

using namespace jsonrpc;

//Mutex NodeConnParamsManagerSSL::m_mutexminer;

NodeParams::NodeParams(const std::string & json)
{

    try {
        js::mValue val;
        js::read_string(json, val);
        js::mObject jsObj = val.get_obj();
        nodeid = jsObj["nodeid"].get_str();
        name = jsObj["name"].get_str();
        agency = jsObj["agency"].get_str();
        cahash = jsObj["cahash"].get_str();
        idx = jsObj["idx"].get_int();
       
    }
    catch (...)
    {
        LOG(WARNING) << "NodeParams format error: " << json ;
    }
}

/**
  读取节点连接配置
  */
NodeConnParamsManagerSSL::NodeConnParamsManagerSSL()
{
   
    try {
        string json = asString( contents(getDataDir() + "/bootstrapnodes.json") );
        js::mValue val;
        js::read_string(json, val);
        js::mObject jsObj = val.get_obj();
        if( jsObj.count("nodes") )
        {
            for (auto node : jsObj["nodes"].get_array())
            {
                if(  node.get_obj()["host"].get_str().empty() ||  node.get_obj()["p2pport"].get_str().empty() )
                    continue;

                string host;
                uint16_t p2pport = -1;
                host = node.get_obj()["host"].get_str();
                p2pport = uint16_t (std::stoi(node.get_obj()["p2pport"].get_str()));
                
                LOG(INFO) << "bootstrapnodes host :" <<host<<",p2pport :"<<p2pport;
                //NodeIPEndpoint nodeendpoint=NodeIPEndpoint(bi::address::from_string(host), p2pport,p2pport);
                NodeIPEndpoint nodeendpoint(host, p2pport,p2pport);
                if(  nodeendpoint.address.to_string() != "0.0.0.0" )
                    m_connectnodes[nodeendpoint.name()] = nodeendpoint;
            }
        }
    }
    catch (...)
    {
        LOG(ERROR) << "NodeConnParamsManagerSSL Parse bootstrapnodes.json Fail! "  ;
        exit(-1);
    }
    if( m_connectnodes.size() )
        LOG(INFO) << "NodeConnParamsManagerSSL Parse bootstrapnodes.json Suc! " << m_connectnodes.size()  ;
    else
    {
        LOG(INFO) << "NodeConnParamsManagerSSL Parse bootstrapnodes.json Empty! Please Add Some Node Message!"  ;
    }
    
    if (m_pContractApi != nullptr) {
        callSysContractData(-1);
    }

    LOG(INFO) << "loadNodeParams m_minernodes size is : " << m_minernodes.size() ;
}


void NodeConnParamsManagerSSL::setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi)
{
    if (m_pContractApi == nullptr)
    {
        m_pContractApi = sysContractApi;

        m_pContractApi->addCBOn("node", [ = ](string) {
            LOG(TRACE) << "receive systemcontract node call";
            callSysContractData(-1);
        });
      
        m_pContractApi->addCBOn("ca", [ = ](string hash) {
            LOG(TRACE) << "receive systemcontract ca call "<<hash;
            caModifyCallback(hash);
        });

        callSysContractData(-1);
    }
}

void NodeConnParamsManagerSSL::callSysContractData(int const& blockNum)
{
    std::vector< NodeParams> vNodeParams;
    m_pContractApi->getAllNode(blockNum, vNodeParams);
    LOG(TRACE) << "call systemcontract get all Nodes  size is " << vNodeParams.size() << ".block is " << blockNum;
   
    Guard l(m_mutexminer);
    {
        if (vNodeParams.size() > 0)
        {
            m_minernodes.clear();
            for (auto stNode : vNodeParams)
            {
                m_minernodes[stNode.nodeid] = stNode;
            }  
        }
        
    }
}

std::pair<bool, std::map<std::string, NodeParams> > NodeConnParamsManagerSSL::getGodMiner(int blockNum)const
{
    std::map<std::string, NodeParams> temp;

    if ( m_chainParams.godMinerStart > 0 )
    {
        u256 lastBlockNumber = blockNum;
        if (blockNum < 0)
        {
            lastBlockNumber = m_pContractApi->getBlockChainNumber();
        }
        if ( ( lastBlockNumber >= (m_chainParams.godMinerStart - 1)  ) && ( lastBlockNumber < m_chainParams.godMinerEnd) )
        {
            LOG(INFO) << " getAllNodeConnInfo lastBlockNumber=" << lastBlockNumber << ",blockNum=" << blockNum << " Hit GodMiner ";
            temp = m_chainParams.godMinerListSSL;
            return std::make_pair(true, temp);
        }
        else
        {
            LOG(WARNING) << "NodeConnParamsManagerSSL::getAllNodeConnInfo lastBlockNumber=" << lastBlockNumber << ",blockNum=" << blockNum << " Don't Hit GodMiner ";
        }
    }

    return std::make_pair(false, temp);
}

void NodeConnParamsManagerSSL::getAllNodeConnInfo(int blockNum, std::map<std::string, NodeConnParams> & mNodeConnParams) const 
{
    std::map<std::string, NodeParams>  nodeParams;
    getAllNode(blockNum,nodeParams);
    for (auto stNode : nodeParams)
    {
        mNodeConnParams[stNode.second.nodeid] = stNode.second;
    } 

}
void NodeConnParamsManagerSSL::getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const 
{
    Guard l(m_mutexminer);
    {
        if (m_minernodes.size() > 0)
        {
            for (auto stNode : m_minernodes)
            {
                mNodeConnParams[stNode.second.nodeid] = stNode.second;
            }  
        }
    }
}

//获取所有的参与共识节点的数据
void NodeConnParamsManagerSSL::getAllNode(int blockNum, std::map<std::string, NodeParams> & mNodeParams) const
{
    LOG(TRACE) << "NodeConnParamsManagerSSL::getAllNode blockNum=" << blockNum;

    std::pair<bool, std::map<std::string, NodeParams> > checkGodMiner = getGodMiner(blockNum);
    if ( checkGodMiner.first == true )
    {
        mNodeParams = checkGodMiner.second;
        return;
    }

    if (blockNum < 0) {
        Guard l(m_mutexminer);
        {
            if (m_minernodes.size() > 0)
            {
                for (auto stNode : m_minernodes)
                {
                    mNodeParams[stNode.second.nodeid] = stNode.second;
                }  
            }
        }
    }
    else {
        std::vector<NodeParams> vNodeParams;
    
        m_pContractApi->getAllNode(blockNum, vNodeParams);
        LOG(TRACE) << "m_pContractApi->getAllNode= " << vNodeParams.size();

        if (vNodeParams.size() > 0)
        {
            for (auto node : vNodeParams)
            {
                mNodeParams[node.nodeid] = node;
            }
        }
       
    }

    if( mNodeParams.size()<1  )
    {
        int idx = 0;
        for (auto nodeid : m_initnodes)
        {
            NodeParams node;
            node.nodeid = nodeid;
            node.idx = idx++;
            mNodeParams[nodeid] = node;
        }
    }
}

void NodeConnParamsManagerSSL::getAllConnect( std::map<std::string, NodeIPEndpoint> & mConnectParams) const 
{
	Guard l(m_mutexconnect);
	for (auto stNode : m_connectnodes)
	{
		mConnectParams[stNode.second.name()] = stNode.second;
	}  
	
}

void NodeConnParamsManagerSSL::connNode(const ConnectParams &param)
{
}

void NodeConnParamsManagerSSL::disconnNode(const std::string & sNodeId)
{
}


bool NodeConnParamsManagerSSL::getPublicKey(u256 const& _idx, Public & _pub) const {
    std::pair<bool, std::map<std::string, NodeParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) 
    {
        for (auto iter = checkGodMiner.second.begin(); iter != checkGodMiner.second.end(); ++iter) {
            if (iter->second.idx == _idx) {
                _pub = jsToPublic(toJS(iter->second.nodeid));
                return true;
            }
        }
        return false;
    }
    else
    {
        Guard l(m_mutexminer);
        for (auto iter = m_minernodes.begin(); iter != m_minernodes.end(); ++iter) {
            if (iter->second.idx == _idx) {
                _pub = jsToPublic(toJS(iter->second.nodeid));
                return true;
            }
        }
        if( _idx <m_initnodes.size() )
        {
             _pub = jsToPublic(toJS(m_initnodes[(uint)_idx]));
             return true;
        }
        return false;
    }
}

bool NodeConnParamsManagerSSL::getIdx(p2p::NodeID const& _nodeId, u256 &_idx) const {
    std::pair<bool, std::map<std::string, NodeParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) 
    {
        auto iter = checkGodMiner.second.find(_nodeId.hex());
        if (iter != checkGodMiner.second.end())
        {
            _idx = iter->second.idx;
            return true;
        }
        return false;
    }
    else
    {
        Guard l(m_mutexminer);
        auto iter = m_minernodes.find(_nodeId.hex());
        if (iter != m_minernodes.end()) 
        {
            _idx = iter->second.idx;
            return true;
        }
    }
    
    for (u256 i = 0;i< m_initnodes.size();i++)
    {
        if( m_initnodes[(uint)i] == _nodeId.hex() )
        {
            _idx = i;
            return true;
        }
    }

    return false;
}

unsigned NodeConnParamsManagerSSL::getMinerNum() const {
    return getNodeNum();

}

bool NodeConnParamsManagerSSL::getAccountType(p2p::NodeID const& _nodeId, unsigned & _type) const {
    std::pair<bool, std::map<std::string, NodeParams> > checkGodMiner = getGodMiner(-1);
    _type=EN_ACCOUNT_TYPE_NORMAL;
    if ( checkGodMiner.first == true ) 
    {
        std::string _nodeIdStr = _nodeId.hex();
        auto iter = checkGodMiner.second.find(_nodeId.hex());
        if (iter != checkGodMiner.second.end()) {
            _type = EN_ACCOUNT_TYPE_MINER;
            return true;
        }

        return false;
    }
    else
    {
        Guard l(m_mutexminer);
        std::string _nodeIdStr = _nodeId.hex();
        auto iter = m_minernodes.find(_nodeId.hex());
        if (iter != m_minernodes.end()) {
            _type=EN_ACCOUNT_TYPE_MINER;
            return true;
        }
    }

    if( m_minernodes.size() < 1 )
    {
        for (u256 i=0;i< m_initnodes.size();i++)
        {
            if( m_initnodes[(uint)i] == _nodeId.hex() )
            {
                _type = EN_ACCOUNT_TYPE_MINER;
                return true;
            }
        }
    }
   

    return false;
}

unsigned NodeConnParamsManagerSSL::getNodeNum() const {
    std::pair<bool, std::map<std::string, NodeParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) 
    {
        return checkGodMiner.second.size();
    }
    else
    {
        Guard l(m_mutexminer);
        return m_minernodes.size()?m_minernodes.size():m_initnodes.size();
    }
}

bool NodeConnParamsManagerSSL::nodeInfoHash(h256& h) 
{
       
        //crypto Common中进行加密
    RLPStream _rlps;
    _rlps.appendList(3) << m_self.nodeid << m_self.agency << m_self.cahash;
    h = dev::sha3(_rlps.out());
    LOG(TRACE) << " getSelfNodeInfo hash is " << h.abridged() << "|nodeid is " << m_self.nodeid.substr(0, 6) << "|agency is " << m_self.agency.substr(0, 6) << "|cahash is " << m_self.cahash ;
    return true;
}

bool NodeConnParamsManagerSSL::checkCertOut(const std::string& serialNumber)
{
    eth::CaInfo caInfo;
    std::shared_ptr<eth::SystemContractApi> pSysContractApi = getSysContractApi();
    pSysContractApi->getCaInfo(serialNumber, caInfo);

    LOG(TRACE) << "serialNumber:" << serialNumber << "|cainfo:" << caInfo.toString();
    if (caInfo.serial == serialNumber)
    {
        LOG(INFO) << "serialNumber:" << serialNumber << "|cainfo:" << caInfo.toString() << ", Cert Has Out!";
        return true;
    }
    return false;
}

void NodeConnParamsManagerSSL::caModifyCallback(const std::string& hash)
{
    eth::CaInfo caInfo;
    std::shared_ptr<eth::SystemContractApi> pSysContractApi = getSysContractApi();
    pSysContractApi->getCaInfo(hash, caInfo);
    LOG(TRACE) << "caModifyCallback getCaInfo, number:" << hash << ",info:" << caInfo.toString()  ;

    if ( ! caInfo.serial.empty() )
    {
        LOG(INFO) << "caModifyCallback this CA must be disconnect ("<<caInfo.pubkey<<")" ;
        m_host->disconnectByNodeId(caInfo.pubkey);
    }

    return;
}

void NodeConnParamsManagerSSL::SetHost(std::shared_ptr<p2p::HostApi> host)
{
    m_host = host;
};

Mutex NodeConnParamsManagerSSL::m_mutexminer;
Mutex NodeConnParamsManagerSSL::m_mutexconnect;
