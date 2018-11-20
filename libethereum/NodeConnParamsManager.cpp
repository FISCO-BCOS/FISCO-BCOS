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
 * @file: NodeConnParamsManager.cpp
 * @author: toxotguo
 * @date: 2018
 */

#include <json_spirit/JsonSpiritHeaders.h>
#include <libethcore/CommonJS.h>
#include <libp2p/Common.h>
#include <libweb3jsonrpc/JsonHelper.h>
#include <libwebthree/WebThree.h>
#include <libp2p/SessionWBCAData.h>
#include <libdevcore/easylog.h>
#include "EthereumHost.h"
#include "NodeConnParamsManager.h"
#include "SystemContractApi.h"

#include "libdevcore/Hash.h"
#include <fstream>
#include "libdevcrypto/Rsa.h"

using namespace std;
using namespace dev;
using namespace eth;
namespace js = json_spirit;

//Mutex NodeConnParamsManager::_xNodeConnParam;

NodeConnParams::NodeConnParams(const std::string & json)
{

    try {
        js::mValue val;
        js::read_string(json, val);
        js::mObject jsObj = val.get_obj();
        _sNodeId = jsObj["Nodeid"].get_str();
        _sAgencyInfo = jsObj["Agencyinfo"].get_str();
        _sIP = jsObj["Peerip"].get_str();
        _iPort = jsObj["Port"].get_int();
        _iIdentityType = jsObj["Identitytype"].get_int();
        _sAgencyDesc = jsObj["Nodedesc"].get_str();
        _iIdx = jsObj["Idx"].get_int();
    }
    catch (...)
    {
        LOG(WARNING) << "NodeConnParams format error: " << json ;
    }


}


NodeConnParamsManager::NodeConnParamsManager(std::string const& _json)
{
    //LOG(INFO) << "NodeConnParamsManager json is " << _json ;
    if (_json == "") {
        return;
    }
    _sConfig = _json;
    js::mValue val;
    js::read_string(_sConfig, val);
    js::mObject obj = val.get_obj();


    for (auto node : obj["NodeextraInfo"].get_array()) {
        NodeConnParams   nodeConnParam;

        nodeConnParam._sNodeId = node.get_obj()["Nodeid"].get_str();
        nodeConnParam._sAgencyInfo = node.get_obj()["Agencyinfo"].get_str();
        nodeConnParam._sIP = node.get_obj()["Peerip"].get_str();
        nodeConnParam._iPort = node.get_obj()["Port"].get_int();
        nodeConnParam._iIdentityType = node.get_obj()["Identitytype"].get_int();
        nodeConnParam._sAgencyDesc = node.get_obj()["Nodedesc"].get_str();
        nodeConnParam._iIdx = u256(node.get_obj()["Idx"].get_int());

        _mConfNodeConnParams[nodeConnParam._sNodeId] = nodeConnParam;

        _mNodeConnParams[nodeConnParam._sNodeId] = nodeConnParam;
    }

    LOG(INFO) << "loadNodeConnParams _mConfNodeConnParams size is : " << _mConfNodeConnParams.size() ;

    if (m_pContractApi != nullptr) {
        callSysContractData(-1);
    }

    LOG(INFO) << "loadNodeConnParams _mNodeConnParams size is : " << _mNodeConnParams.size() ;

}

bool NodeConnParamsManager::CAVerify = false;


void NodeConnParamsManager::setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi)
{
    if (m_pContractApi == nullptr)
    {
        m_pContractApi = sysContractApi;
        m_pContractApi->addCBOn("node", [ = ](string) {
            LOG(INFO) << "receive systemcontract node call";
            callSysContractData(-1);
        });
        callSysContractData(-1);
    }
}

void NodeConnParamsManager::callSysContractData(int const& blockNum)
{
    std::vector< NodeConnParams> vNodeParams;
    m_pContractApi->getAllNode(blockNum, vNodeParams);

    LOG(INFO) << "call systemcontract get all Nodes  size is " << vNodeParams.size() << ".block is " << blockNum;
   
    if (vNodeParams.size() > 0 && checkNodesValid(vNodeParams))
    {

        std::vector< NodeConnParams> vAddNodes;
        std::vector< NodeConnParams> vDelNodes;

        {
            Guard l(_xNodeConnParam);
            diffNodes(vNodeParams, vAddNodes, vDelNodes);

            LOG(INFO) << "call systemcontract diff nodes. add nodes size is " << vAddNodes.size() << ".del nodes size is " << vDelNodes.size() ;
        
            if ( vNodeParams.size() > 0 ) 
            {
                _mNodeConnParams.clear();
                for (auto stNode : vNodeParams)
                {
                    _mNodeConnParams[stNode._sNodeId] = stNode;
                }
            }


        }

        {
           
            std::map<std::string, NodeConnParams> mNodeConnParams;
            getAllConfNodeConnInfo(mNodeConnParams);
            for (auto stDelNode : vDelNodes)
            {
                if (mNodeConnParams.find(stDelNode._sNodeId) == mNodeConnParams.end())
                {
                    disconnNode(stDelNode._sNodeId);
                }
            }

            
            for (auto stAddNode : vAddNodes)
            {
                if (mNodeConnParams.find(stAddNode._sNodeId) == mNodeConnParams.end())
                {
                    connNode(stAddNode);
                }
            }
        }
    }
}

bool NodeConnParamsManager::checkNodesValid(const std::vector< NodeConnParams> &vNodes)
{
    bool rRet = false;
    if (vNodes.size() == 0)
    {
        return rRet;
    }

   
    int i = 0;
    bool bContinuous = true;
    dev::u256 iMaxSignNodeIdx = -1;
    dev::u256 iMaxNotSignNodeIdx = -1;
    for (auto node : vNodes)
    {
        LOG(INFO) << "checkNodesValid : " << node.toString() ;
        if (node._iIdx != i)
        {
            bContinuous = false;
            break;
        }
        i++;

       
        if (node._iIdentityType == AccountType::EN_ACCOUNT_TYPE_MINER)
        {
            iMaxSignNodeIdx = node._iIdx;
        }
        else {
            iMaxNotSignNodeIdx = node._iIdx;
        }
    }
    if (!bContinuous)
    {
        return rRet;
    }

    if (iMaxSignNodeIdx < iMaxNotSignNodeIdx || iMaxNotSignNodeIdx == -1)
    {
        rRet = true;
    }

    LOG(INFO) << "checkNodesValid is " << rRet ;
    return rRet;
}


bool NodeConnParamsManager::diffNodes(const std::vector< NodeConnParams> &vNodeParams, std::vector< NodeConnParams> &vAddNodes, std::vector< NodeConnParams> &vDelNodes)
{
    if (vNodeParams.size() == 0)
    {
        return false;
    }

    std::map<std::string, NodeConnParams> mTmpNode;
    
    for (auto stNewNode : vNodeParams)
    {
        if (_mNodeConnParams.find(stNewNode._sNodeId) == _mNodeConnParams.end())
        {
            vAddNodes.push_back(stNewNode);
        }
        mTmpNode[stNewNode._sNodeId] = stNewNode;
    }

    
    for (auto stOldNode : _mNodeConnParams)
    {
        if (mTmpNode.find(stOldNode.first) == mTmpNode.end())
        {
            vDelNodes.push_back(stOldNode.second);
        }
    }

    return vAddNodes.size() > 0 || vDelNodes.size() > 0;
}

std::pair<bool, std::map<std::string, NodeConnParams> > NodeConnParamsManager::getGodMiner(int blockNum)const
{
    std::map<std::string, NodeConnParams> temp;

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

            temp = m_chainParams.godMinerList;
            return std::make_pair(true, temp);
        }
        else
        {
            LOG(WARNING) << "NodeConnParamsManager::getAllNodeConnInfo lastBlockNumber=" << lastBlockNumber << ",blockNum=" << blockNum << " Don't Hit GodMiner ";
        }
    }

    return std::make_pair(false, temp);
}

void NodeConnParamsManager::getAllNodeConnInfo(int blockNum, std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
    LOG(TRACE) << "NodeConnParamsManager::getAllNodeConnInfo blockNum=" << blockNum;

    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(blockNum);
    if ( checkGodMiner.first == true )
    {
        mNodeConnParams = checkGodMiner.second;
        return;
    }

    if (blockNum < 0) {
        getAllNodeConnInfoContract(mNodeConnParams);
    }
    else {
        std::vector<NodeConnParams> vNodeParams;
        m_pContractApi->getAllNode(blockNum, vNodeParams);
        LOG(TRACE) << "m_pContractApi->getAllNode= " << vNodeParams.size();

        if (vNodeParams.size() > 0)
        {
            for (auto node : vNodeParams)
            {
                mNodeConnParams[node._sNodeId] = node;
            }
        }
        else
        {
            int idx = 0;
            for (auto nodeid : m_initnodes)
            {
                NodeConnParams node;
                node._sNodeId = nodeid;
                node._iIdx = idx++;
                node._iIdentityType = 1; 
                mNodeConnParams[nodeid] = node;
            }
        }
    }
}

bool NodeConnParamsManager::addNewNodeConnInfo(const std::string &_json)
{
    bool bRet = false;
    if (_json == "")
    {
        return bRet;
    }

    js::mValue val;
    js::read_string(_json, val);
    js::mObject obj = val.get_obj();

    NodeConnParams   nodeConnParam;
    nodeConnParam._sNodeId = obj["Nodeid"].get_str();
    nodeConnParam._sAgencyInfo = obj["Agencyinfo"].get_str();
    nodeConnParam._sIP = obj["Peerip"].get_str();
    nodeConnParam._iPort = obj["Port"].get_int();
    nodeConnParam._iIdentityType = obj["Identitytype"].get_int();
    nodeConnParam._sAgencyDesc = obj["Nodedesc"].get_str();
    nodeConnParam._iIdx = obj["Idx"].get_int();

    Guard l(_xConfigNodeConnParam);
    if (_mConfNodeConnParams.find(nodeConnParam._sNodeId) != _mConfNodeConnParams.end())
    {
        if (!(_mConfNodeConnParams[nodeConnParam._sNodeId] == nodeConnParam))
        {
            LOG(INFO) << "new param existed .but not the same . old is " << _mConfNodeConnParams[nodeConnParam._sNodeId].toString() << "| new is " << nodeConnParam.toString() ;
        }
    }
    else
    {
        _mConfNodeConnParams[nodeConnParam._sNodeId] = nodeConnParam;
        writeNodeFile();
        bRet = true;
    }

    return bRet;
}


bool NodeConnParamsManager::addNewNodeConnInfo(const NodeConnParams &nodeParam)
{
    Guard l(_xConfigNodeConnParam);
    if (_mConfNodeConnParams.find(nodeParam._sNodeId) == _mConfNodeConnParams.end())
    {
        _mConfNodeConnParams[nodeParam._sNodeId] = nodeParam;

        writeNodeFile();
    }

    return true;
}

void NodeConnParamsManager::sendNodeInfoSync(const std::vector<NodeConnParams> &vParams)
{
    if (vParams.size() == 0 || m_phost == nullptr)
    {
        return;
    }
    LOG(INFO) << "sendNodeInfoSync  " << vParams.size() ;
    m_phost->addNodeConnParam(vParams);
}

void NodeConnParamsManager::connNode(const NodeConnParams &param)
{
    LOG(TRACE) << "NodeConnParamsManager::connNode" << param.toEnodeInfo() << " Valid=" << param.Valid();

    if (m_pnetwork == nullptr || !param.Valid())
    {
        LOG(TRACE) << "NodeConnParamsManager::connNode Not Valid!";
        return;
    }

    try {
        LOG(TRACE) << "NodeConnParamsManager::connNode start" << param.toEnodeInfo() << " Valid=" << param.Valid();
        m_pnetwork->addPeer(p2p::NodeSpec(param.toEnodeInfo()), p2p::PeerType::Required);
    }
    catch (...)
    {
        LOG(WARNING) << "NodeConnParamsManager::connNode network addPeer error.  enode is " << param.toEnodeInfo() ;
    }
}

void NodeConnParamsManager::sendDelNodeInfoSync(const std::string &sNodeId)
{
    if (sNodeId == "" || m_phost == nullptr)
    {
        return;
    }

    m_phost->delNodeConnParam(sNodeId);
}

void NodeConnParamsManager::delNodeConnInfo(const std::string &sNodeId, bool &bExisted)
{
    bExisted = false;
    Guard l(_xConfigNodeConnParam);
    if (_mConfNodeConnParams.find(sNodeId) != _mConfNodeConnParams.end())
    {
        LOG(INFO) << "NodeConnParamsManager::delNodeConnInfo del node ：" << sNodeId ;

        auto ite = _mConfNodeConnParams.find(sNodeId);
        _mConfNodeConnParams.erase(ite);
        writeNodeFile();
        bExisted = true;
    }
    else
    {
        LOG(INFO) << "NodeConnParamsManager::delNodeConnInfo no node : " << sNodeId ;
    }
}


void NodeConnParamsManager::writeNodeFile()
{
    Json::Value resConn;
    Json::Reader reader;
    if (reader.parse(_sConfig, resConn))
    {
        resConn.removeMember("NodeextraInfo");
        resConn["NodeextraInfo"] = Json::Value(Json::arrayValue);

        for (auto const & param : _mConfNodeConnParams)
        {
            Json::Value res;
            res["Nodeid"] = param.second._sNodeId;
            res["Agencyinfo"] = param.second._sAgencyInfo;
            res["Peerip"] = param.second._sIP;
            res["Port"] = param.second._iPort;
            res["Identitytype"] = param.second._iIdentityType;
            res["Nodedesc"] = param.second._sAgencyDesc;
            res["Idx"] = static_cast<unsigned>(param.second._iIdx);
            resConn["NodeextraInfo"].append(res);
        }

        writeFile(getConfigPath(), resConn.toStyledString());
        LOG(INFO) << "writeNodeFile succ" ;
    }

}

void NodeConnParamsManager::disconnNode(const std::string & sNodeId)
{
    if (m_pnetwork == nullptr || sNodeId == "")
    {
        return;
    }

    LOG(INFO) << "disconnNode node id is " << sNodeId ;
    try {
        m_pnetwork->disconnectByNodeId(sNodeId);
    }
    catch (...)
    {
        LOG(WARNING) << "NodeConnParamsManager::connNode network disconnect error.  enode is " << sNodeId ;
    }
}

bool NodeConnParamsManager::getPublicKey(u256 const& _idx, Public & _pub) const {
    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) 
    {
        for (auto iter = checkGodMiner.second.begin(); iter != checkGodMiner.second.end(); ++iter) {
            if (iter->second._iIdx == _idx) {
                _pub = jsToPublic(toJS(iter->second._sNodeId));
                return true;
            }
        }
        return false;
    }
    else
    {

        Guard l(_xNodeConnParam);
        for (auto iter = _mNodeConnParams.begin(); iter != _mNodeConnParams.end(); ++iter) {
            if (iter->second._iIdx == _idx) {
                _pub = jsToPublic(toJS(iter->second._sNodeId));
                return true;
            }
        }
        return false;
    }
}

bool NodeConnParamsManager::getIdx(p2p::NodeID const& _nodeId, u256 &_idx) const {
    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) 
    {
        auto iter = checkGodMiner.second.find(_nodeId.hex());
        if (iter != checkGodMiner.second.end()) {
            _idx = iter->second._iIdx;
            return true;
        }
        return false;
    }
    else
    {
        Guard l(_xNodeConnParam);
        auto iter = _mNodeConnParams.find(_nodeId.hex());
        if (iter != _mNodeConnParams.end()) {
            _idx = iter->second._iIdx;
            return true;
        }
        return false;
    }

}

unsigned NodeConnParamsManager::getMinerNum() const {
    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) 
    {
        unsigned count = 0;
        for (auto iter = checkGodMiner.second.begin(); iter != checkGodMiner.second.end(); ++iter) {
            if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
                ++count;
            }
        }
        return count;
    }
    else
    {
        Guard l(_xNodeConnParam);
        unsigned count = 0;
        for (auto iter = _mNodeConnParams.begin(); iter != _mNodeConnParams.end(); ++iter) {
            if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) {
                ++count;
            }
        }
        return count;
    }

}

bool NodeConnParamsManager::getAccountType(p2p::NodeID const& _nodeId, unsigned & _type) const {
    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true )
    {
        std::string _nodeIdStr = _nodeId.hex();
        auto iter = checkGodMiner.second.find(_nodeId.hex());
        if (iter != checkGodMiner.second.end()) {
            _type = iter->second._iIdentityType;
            return true;
        }
        return false;
    }
    else
    {
        Guard l(_xNodeConnParam);
        std::string _nodeIdStr = _nodeId.hex();
        auto iter = _mNodeConnParams.find(_nodeId.hex());
        if (iter != _mNodeConnParams.end()) {
            _type = iter->second._iIdentityType;
            return true;
        }
        return false;
    }
}

unsigned NodeConnParamsManager::getNodeNum() const {
    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true )
    {
        return checkGodMiner.second.size();
    }
    else
    {
        Guard l(_xNodeConnParam);
        return _mNodeConnParams.size();
    }
}

bool NodeConnParamsManager::nodeInfoHash(h256& h) 
{
        eth::NodeConnParams stNodeConnParam;
        std::string sNodeId = m_host->id().hex();
        if (!getNodeConnInfoBoth(sNodeId, stNodeConnParam))
        {
            LOG(INFO) << "No NodeConninfo (" << sNodeId << ")." ;
            return false;
        }
       
        RLPStream _rlps;
    _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
    // _rlps.appendList(2) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo;//不使用ip进行数字签名操作
    h = dev::sha3(_rlps.out());
    LOG(INFO) << " getSelfNodeInfo hash is " << h.abridged() << "|nodeid is " << stNodeConnParam._sNodeId.substr(0, 6) << "|agenceinfo is " << stNodeConnParam._sAgencyInfo.substr(0, 6) << "|ip is " << stNodeConnParam._sIP ;
    return true;
}

bool NodeConnParamsManager::signNodeInfo(CABaseData & caBaseData)
{
    try {
        h256 hash;
        if (!nodeInfoHash(hash))
            return false;

        caBaseData.setNodeSign(sign(m_host->sec(), hash));
        return true;
    }
    catch (...)
    {
        LOG(WARNING) << "getSelfSignData throws exceptions. nodeId is " << m_host->id().hex() ;
    }
    return false;
}

bool NodeConnParamsManager::signCAInfo(std::string seedStr, CABaseData & caBaseData)
{
    try
    {
        std::string privateKeyFile = getDataDir() + "/CA/private.key";
        std::string pubFile = getDataDir() + "/CA/public.key";
        std::ifstream pubFileStream(pubFile.c_str());
        std::ifstream privateFileStream(privateKeyFile.c_str());
        if (pubFileStream.is_open() && privateFileStream.is_open())
        {
            std::string signMsg = dev::crypto::RSAKeySign(privateKeyFile, seedStr);
            if (signMsg.empty())
            {
                LOG(INFO) << "RSAKeySign failed!" ;
                return false;
            }
            //get sha256
            std::string content((std::istreambuf_iterator<char>(pubFileStream)), std::istreambuf_iterator<char>());
            pubFileStream.close();
            privateFileStream.close();
            bytes tmp1(content.begin(), content.end());
            bytesConstRef bcr(tmp1.data(), tmp1.size());
            std::string hexStr = dev::sha256(bcr).hex();

            caBaseData.setSeed(seedStr);
            caBaseData.setPub256(hexStr);
            caBaseData.setSign(signMsg);
        }
    } catch (...)
    {
        LOG(WARNING) << "catch exception in RSAKeySign" ;
    }

    return true;
}

bool NodeConnParamsManager::checkNodeInfo(const string& sEndPointNodeId, const h256& h) 
{
    bool bRet = false;
    LOG(INFO) << "checkNodeConnInfo nodeid is " << sEndPointNodeId.substr(0, 6) << "| nodeinfo hash is " << h.abridged();
    
    NodeConnParams stNodeConnParam;
    if (!getNodeConnInfoBoth(sEndPointNodeId, stNodeConnParam))
    {
        LOG(WARNING) << "No NodeConninfo (" << sEndPointNodeId.substr(0, 6) << ")." ;
        return bRet;
    }

    RLPStream _rlps;
    _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
    auto hash = dev::sha3(_rlps.out());

    LOG(INFO) << "checkNodeConnInfo(from local data) hash is " << hash.abridged() << "|" << stNodeConnParam._sNodeId.substr(0, 6) << "|" << stNodeConnParam._sAgencyInfo.substr(0, 6) << "|" << stNodeConnParam._sIP ;    
    
    if (hash != h)
    {
        LOG(WARNING) << "NodeInfo hash error. (" << sEndPointNodeId.substr(0, 6) << ")'s hash is " << h.abridged() << "." ;
        return bRet;
    }

    bRet = true;
    return bRet;
}
// DEPRECATED
bool NodeConnParamsManager::checkNodeInfo(const std::string& sEndPointNodeId, CABaseData & caBaseData)
{
    bool bRet = false;
    // std::string sEndPointNodeId = remoteNodeID;
    LOG(INFO) << " checkNodeConnInfo nodeid is " << sEndPointNodeId << "| sign is " << caBaseData.getNodeSign() ;
   
    NodeConnParams stNodeConnParam;
    if (!getNodeConnInfoBoth(sEndPointNodeId, stNodeConnParam))
    {
        LOG(WARNING) << "No NodeConninfo (" << sEndPointNodeId << ")." ;
        return bRet;
    }

   
    RLPStream _rlps;
    _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
	
    auto hash = dev::sha3(_rlps.out());

    LOG(INFO) << "checkNodeConnInfo hash is " << hash << "|" << stNodeConnParam._sNodeId << "|" << stNodeConnParam._sAgencyInfo << "|" << stNodeConnParam._sIP;
   
    if (!verify(Public(sEndPointNodeId), caBaseData.getNodeSign(), hash))
    {
        LOG(WARNING) << "Sign error. (" << sEndPointNodeId << ") sSign is " << caBaseData.getNodeSign() << "." ;
        return bRet;
    }

    bRet = true;
    return bRet;
}

bool NodeConnParamsManager::checkIP(std::string remoteNodeID, CaInfo &caInfo)
{
    eth::NodeConnParams params;
    bool found = getNodeConnInfoBoth(remoteNodeID, params);
    if (!found)
    {
        LOG(INFO) << "not found params in getNodeConnInfo,remoteNodeID:" << remoteNodeID ;
        return false;
    }
    std::string remoteIp = params._sIP;

    if (!caInfo.black.empty())
    {
        for (std::string blackIp : caInfo.getBlackList())
        {
            if (blackIp == remoteIp)
            {
                LOG(INFO) << "remoteIp is in black list.remoteIp:" << remoteIp ;
                return false;
            }
        }
    }

    if (!caInfo.white.empty())
    {
        found = false;
        for (std::string whiteIp : caInfo.getWhiteList())
        {
            if (whiteIp == remoteIp)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            LOG(INFO) << "remoteIp is not in white list.remoteIp:" << remoteIp ;
            return false;
        }
    }

    return true;
}

bool NodeConnParamsManager::CheckConnectCert(const std::string& serialNumber, const std::string& /*ip*/)
{
    bool caVerify = eth::NodeConnParamsManager::CAVerify;
    if (!caVerify)
    {
        LOG(INFO) << "CAVerify is false." ;
        return true;
    }

    eth::CaInfo caInfo;
    std::shared_ptr<eth::SystemContractApi> pSysContractApi = getSysContractApi();
    pSysContractApi->getCaInfo(serialNumber, caInfo);

    LOG(INFO) << "serialNumber:" << serialNumber << "|cainfo:" << caInfo.toString();

    if (caInfo.blocknumber == 0 || caInfo.status != eth::CaStatus::Ok)
    {
        LOG(INFO) << "blockNumber is 0 or status is false." ;
        return false;
    }

    if (caInfo.hash != serialNumber)
    {
        LOG(INFO) << "certSerialNum is not in contract";
        return false;
    }

    return true;
}

// DEPRECATED
bool NodeConnParamsManager::checkCA(std::string remoteNodeID, CABaseData & caBaseData)
{
    bool caVerify = eth::NodeConnParamsManager::CAVerify;
    if (!caVerify)
    {
        LOG(INFO) << "CAVerify is false." ;
        return true;
    }

    std::string seedStr = caBaseData.getSeed();
    if (seedStr.empty())
    {
        LOG(INFO) << "seedStr is empty.remoteNodeID:" << remoteNodeID ;
        return false;
    }

    std::string pub256 = caBaseData.getPub256();
    std::string sign = caBaseData.getSign();
    if (pub256.empty() || sign.empty())
    {
        LOG(INFO) << "pub256 or sign is empty." ;
        return false;
    }

    eth::CaInfo caInfo;
    std::shared_ptr<eth::SystemContractApi> pSysContractApi = getSysContractApi();
    pSysContractApi->getCaInfo(pub256, caInfo);
    LOG(INFO) << "getCaInfo, key:" << pub256 << ",info:" << caInfo.toString() << ",CAVerify:" << caVerify ;

    if (caInfo.blocknumber == 0 || caInfo.status != eth::CaStatus::Ok)
    {
        LOG(INFO) << "blockNumber is 0 or status is false." ;
        return false;
    }

    bool ok = checkIP(remoteNodeID, caInfo);
    if (!ok)
    {
        LOG(INFO) << "checkIP is false.remoteNodeID:" << remoteNodeID ;
        return false;
    }

    std::string message = dev::crypto::RSAKeyVerify(caInfo.pubkey, sign);
    if (message.empty())
    {
        LOG(INFO) << "RSAKeyVerify message is empty" ;
        return false;
    }

    if (seedStr != message)
    {
        LOG(INFO) << "pubkey verify false,seed:" << seedStr  << ",message:" << message << ",remoteNodeID:" << remoteNodeID ;
        return false;
    }

    LOG(INFO) << "verify ok!!!" ;
    return true;
}

// DEPRECATED
bool NodeConnParamsManager::CheckAll(const std::string& sNodeId, CABaseData &caBaseData) {
    return checkNodeInfo(sNodeId, caBaseData) && checkCA(sNodeId, caBaseData);
}
// DEPRECATED
void NodeConnParamsManager::ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd)
{
    std::string seedStr(_rbd.getSeed().begin(), _rbd.getSeed().end());
    if (seedStr.empty())
    {
        LOG(WARNING) << "seedStr is empty.nodeId:" << m_host->id().hex() ;
        return;
    }

    WBCAData wbCAData;
    bool ok = signNodeInfo(wbCAData);
    if (!ok)
    {
        LOG(WARNING) << "signNodeInfo false!!! nodeId:" << m_host->id().hex() ;
        return;
    }
    ok = signCAInfo(seedStr, wbCAData);
    if (!ok)
    {
        LOG(WARNING) << "signCAInfo false!!! nodeId:" << m_host->id().hex() ;
        return;
    }

    _rlp.append((unsigned)HelloPacket).appendList(8)
            << dev::p2p::c_protocolVersion
            << m_host->nodeInfo().version
            << m_host->caps()
            << m_host->listenPort()
            << m_host->id()
            << wbCAData.getNodeSign()
            << wbCAData.getSign()
            << wbCAData.getPub256();

    LOG(INFO) << "ConstructHandShakeRLP, pub256:" << wbCAData.getPub256() << ",nodeSign:" << wbCAData.getNodeSign() << ",nodeId:" << m_host->id().hex() ;
}
// DEPRECATED
//save data to session
void NodeConnParamsManager::SaveCADataInSession(const std::string nodeId, CABaseData &caBaseData)
{
    if (nodeId.empty())
    {
        return;
    }

    m_host->saveCADataByNodeId(nodeId, caBaseData);
}

void NodeConnParamsManager::CAInfoModifyCallback(const std::string& pub256)
{
    bool caVerify = eth::NodeConnParamsManager::CAVerify;
    if (!caVerify)
    {
        LOG(INFO) << "CAInfoModifyCallback CAVerify is false." ;
        return;
    }

    eth::CaInfo caInfo;
    std::shared_ptr<eth::SystemContractApi> pSysContractApi = getSysContractApi();
    pSysContractApi->getCaInfo(pub256, caInfo);
    LOG(INFO) << "CAInfoModifyCallback getCaInfo, key:" << pub256 << ",info:" << caInfo.toString() << ",CAVerify:" << caVerify ;

    if (caInfo.blocknumber == 0 || caInfo.status != eth::CaStatus::Ok)
    {
        LOG(INFO) << "blockNumber is 0 or status is false." ;
        //break this connection if connected
        m_host->disconnectByPub256(pub256);
        return;
    }

    return;
}

void NodeConnParamsManager::CAVerifyModifyCallback()
{
    bool caVerify = eth::NodeConnParamsManager::CAVerify;
    if (!caVerify)
    {
        return;
    }

    m_host->recheckAllCA();
};

void NodeConnParamsManager::SetHost(std::shared_ptr<p2p::HostApi> host)
{
    m_host = host;
};




bool NodeConnParamsManager::getNodeConnInfo(std::string const& sNodeID, NodeConnParams &retNode) const
{
	bool bFind = false;
	Guard l(_xNodeConnParam);
	if (_mNodeConnParams.find(sNodeID) != _mNodeConnParams.end())
	{
		bFind = true;
		retNode = _mNodeConnParams[sNodeID];
	}
	return bFind;
}

//从内存中 和config数据中查找 更多的是用于自己的
bool NodeConnParamsManager::getNodeConnInfoBoth(std::string const& sNodeID, NodeConnParams &retNode) const
{
	bool bFind = getNodeConnInfo(sNodeID, retNode);
	if (!bFind)
	{
		Guard l(_xConfigNodeConnParam);
		if (_mConfNodeConnParams.find(sNodeID) != _mConfNodeConnParams.end())
		{
			bFind = true;
			retNode = _mConfNodeConnParams[sNodeID];
		}
	}
	return bFind;
}


void NodeConnParamsManager::getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
	Guard l(_xNodeConnParam);
	mNodeConnParams = _mNodeConnParams;
}


void NodeConnParamsManager::getAllConfNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
	Guard l(_xConfigNodeConnParam);
	mNodeConnParams = _mConfNodeConnParams;
}

void NodeConnParamsManager::getAllNodeConnInfoContractAndConf(std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
	mNodeConnParams.clear();
	{
		Guard l(_xNodeConnParam); 
		mNodeConnParams = _mNodeConnParams;
	}

	{
		Guard l(_xConfigNodeConnParam);
		for (auto node : _mConfNodeConnParams)
		{
			if (mNodeConnParams.find(node.first) == mNodeConnParams.end())
			{
				mNodeConnParams[node.first] = node.second;
			}
		}
	}

}

