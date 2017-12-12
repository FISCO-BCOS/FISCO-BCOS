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
 * @file: NodeConnParamsManager.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
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
        LOG(INFO) << "NodeConnParams format error: " << json ;
    }


}

/**
  读取节点连接配置
  */
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


    //读取连接的节点信息
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

    //读取共识需要的节点数据
    //从合约里面取一次 修改共识的节点数据
    if (_pSysContractApi != nullptr) {
        callSysContractData(-1);
    }

    LOG(INFO) << "loadNodeConnParams _mNodeConnParams size is : " << _mNodeConnParams.size() ;

}

bool NodeConnParamsManager::CAVerify = false;

//设置sysEthereumApi  同时设置回调函数
void NodeConnParamsManager::setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi)
{
    //如果为空这设置
    if (_pSysContractApi == nullptr)
    {
        _pSysContractApi = sysContractApi;

        //注册回调
        _pSysContractApi->addCBOn("node", [ = ](string) {
            LOG(INFO) << "receive systemcontract node call";
            //LOG(INFO) << "receive systemcontract node call" ;
            callSysContractData(-1);
        });

        ////CA callback
        //_pSysContractApi->addCBOn("ca", [ = ](string pub256) {
        //    LOG(INFO) << "receive systemcontract node call";
        //    //LOG(INFO) << "receive systemcontract CA callback" ;
        //    CAInfoModifyCallback(pub256);
        //});

        ////CA verify callback
        //_pSysContractApi->addCBOn("config", [ = ](string) {
        //    LOG(INFO) << "receive systemcontract node call";
        //    //LOG(INFO) << "receive systemcontract CA callback" ;
        //    CAVerifyModifyCallback();
        //});

        callSysContractData(-1);
    }
}

void NodeConnParamsManager::callSysContractData(int const& blockNum)
{
    std::vector< NodeConnParams> vNodeParams;
    //默认获取最高块
    _pSysContractApi->getAllNode(blockNum, vNodeParams);
    LOG(INFO) << "call systemcontract get all Nodes  size is " << vNodeParams.size() << ".block is " << blockNum;
    //LOG(INFO) << "call systemcontract get all Nodes  size is " << vNodeParams.size() << ".block is " << blockNum ;
    //如果取出数据就使用该数据
    if (vNodeParams.size() > 0 && checkNodesValid(vNodeParams))
    {

        std::vector< NodeConnParams> vAddNodes;
        std::vector< NodeConnParams> vDelNodes;

        {
            //更新内存数据，不存在的节点进行断连，新加节点进行连接
            //和共识节点数据进行比较 如果有变化则修改共识节点的数据
            Guard l(_xNodeConnParam);
            diffNodes(vNodeParams, vAddNodes, vDelNodes);

            LOG(TRACE) << "call systemcontract diff nodes. add nodes size is " << vAddNodes.size() << ".del nodes size is " << vDelNodes.size() ;
            //设置合约内的节点数据
            if ( vNodeParams.size() > 0 ) //直接覆盖
            {
                _mNodeConnParams.clear();
                for (auto stNode : vNodeParams)
                {
                    _mNodeConnParams[stNode._sNodeId] = stNode;
                }
            }


        }

        {
            //判断节点是否在配置文件中 如果节点在配置文件中 则不进行断连 因为配置文件还允许连接
            //断掉删除的节点
            std::map<std::string, NodeConnParams> mNodeConnParams;
            NodeConnParamsManagerApi::getAllConfNodeConnInfo(mNodeConnParams);
            for (auto stDelNode : vDelNodes)
            {
                if (mNodeConnParams.find(stDelNode._sNodeId) == mNodeConnParams.end())
                {
                    disconnNode(stDelNode._sNodeId);
                }
            }

            //判断节点是否在配置文件中 如果节点在配置文件中 则不进行新的连接 因为已经有连接呢
            //连接新节点
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

//检查节点是否连续
/*
   检查条件
   1、idx从0开始
   2、idx是连续的
   3、前面是记账者节点，后面是非记账节点
   ps ： 数据出来时保证 idx是递增的，例如1.5.9
   */
bool NodeConnParamsManager::checkNodesValid(const std::vector< NodeConnParams> &vNodes)
{
    bool rRet = false;
    if (vNodes.size() == 0)
    {
        return rRet;
    }

    //从0开始
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

        //用枚举
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


//不支持修改数据， 即不支持重复数据
bool NodeConnParamsManager::diffNodes(const std::vector< NodeConnParams> &vNodeParams, std::vector< NodeConnParams> &vAddNodes, std::vector< NodeConnParams> &vDelNodes)
{
    if (vNodeParams.size() == 0)
    {
        return false;
    }

    std::map<std::string, NodeConnParams> mTmpNode;
    //找到新节点
    for (auto stNewNode : vNodeParams)
    {
        if (_mNodeConnParams.find(stNewNode._sNodeId) == _mNodeConnParams.end())
        {
            vAddNodes.push_back(stNewNode);
        }
        mTmpNode[stNewNode._sNodeId] = stNewNode;
    }

    //找到不存在的节点
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

    if ( _chainParams.godMinerStart > 0 ) //开启了上帝模式
    {
        u256 lastBlockNumber = blockNum;
        if (blockNum < 0)
        {
            lastBlockNumber = _pSysContractApi->getBlockChainNumber();
        }

        if ( ( lastBlockNumber >= (_chainParams.godMinerStart - 1)  ) && ( lastBlockNumber < _chainParams.godMinerEnd) )
        {
            LOG(INFO) << " getAllNodeConnInfo lastBlockNumber=" << lastBlockNumber << ",blockNum=" << blockNum << " Hit GodMiner ";

            temp = _chainParams.godMinerList;
            return std::make_pair(true, temp);
        }
        else
        {
            LOG(WARNING) << "NodeConnParamsManager::getAllNodeConnInfo lastBlockNumber=" << lastBlockNumber << ",blockNum=" << blockNum << " Don't Hit GodMiner ";
        }
    }

    return std::make_pair(false, temp);
}

//获取所有的参与共识节点的数据
void NodeConnParamsManager::getAllNodeConnInfo(int blockNum, std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
    LOG(TRACE) << "NodeConnParamsManager::getAllNodeConnInfo=" << blockNum;

    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(blockNum);
    if ( checkGodMiner.first == true )
    {
        mNodeConnParams = checkGodMiner.second;
        return;
    }

    if (blockNum < 0) {
        NodeConnParamsManagerApi::getAllNodeConnInfoContract(mNodeConnParams);
    }
    else {
        std::vector<NodeConnParams> vNodeParams;
        //默认获取最高块
        //创世块中一定有数据 不考虑为空的情况
        _pSysContractApi->getAllNode(blockNum, vNodeParams);
        LOG(TRACE) << "_pSysContractApi->getAllNode= " << vNodeParams.size();

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
            for (auto nodeid : _vInitIdentityNodes)
            {
                NodeConnParams node;
                node._sNodeId = nodeid;
                node._iIdx = idx++;
                node._iIdentityType = 1; // 默认这里的都是记账节点
                mNodeConnParams[nodeid] = node;
            }
        }
    }
}

//增加新的节点配置
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

//增加新的节点配置
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

//发送配置同步请求
void NodeConnParamsManager::sendNodeInfoSync(const std::vector<NodeConnParams> &vParams)
{
    if (vParams.size() == 0 || _pHost == nullptr)
    {
        return;
    }
    LOG(INFO) << "sendNodeInfoSync  " << vParams.size() ;
    //增加节点配置
    _pHost->addNodeConnParam(vParams);
}

//进行节点连接
void NodeConnParamsManager::connNode(const NodeConnParams &param)
{
    LOG(TRACE) << "NodeConnParamsManager::connNode" << param.toEnodeInfo() << " Valid=" << param.Valid();

    if (_pNetwork == nullptr || !param.Valid())
    {
        LOG(TRACE) << "NodeConnParamsManager::connNode Not Valid!";
        return;
    }

    try {
        LOG(TRACE) << "NodeConnParamsManager::connNode start" << param.toEnodeInfo() << " Valid=" << param.Valid();
        _pNetwork->addPeer(p2p::NodeSpec(param.toEnodeInfo()), p2p::PeerType::Required);
    }
    catch (...)
    {
        LOG(ERROR) << "NodeConnParamsManager::connNode network addPeer error.  enode is " << param.toEnodeInfo() ;
    }
}

//发送删除节点连接信息
void NodeConnParamsManager::sendDelNodeInfoSync(const std::string &sNodeId)
{
    if (sNodeId == "" || _pHost == nullptr)
    {
        return;
    }

    _pHost->delNodeConnParam(sNodeId);
}

//删除节点信息 删除config.json中的数据 并落地
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

//写节点文件
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

//断掉连接
void NodeConnParamsManager::disconnNode(const std::string & sNodeId)
{
    if (_pNetwork == nullptr || sNodeId == "")
    {
        return;
    }

    LOG(INFO) << "disconnNode node id is " << sNodeId ;
    try {
        _pNetwork->disconnectByNodeId(sNodeId);
    }
    catch (...)
    {
        LOG(ERROR) << "NodeConnParamsManager::connNode network disconnect error.  enode is " << sNodeId ;
    }
}

//-------------------------共识需要下面的接口（数据全部从共识中取） start--------------------------------------//
bool NodeConnParamsManager::getPublicKey(u256 const& _idx, Public & _pub) const {
    std::pair<bool, std::map<std::string, NodeConnParams> > checkGodMiner = getGodMiner(-1);
    if ( checkGodMiner.first == true ) //上帝模式
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
    if ( checkGodMiner.first == true ) //上帝模式
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
    if ( checkGodMiner.first == true ) //上帝模式
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
    if ( checkGodMiner.first == true ) //上帝模式
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
    if ( checkGodMiner.first == true ) //上帝模式
    {
        return checkGodMiner.second.size();
    }
    else
    {
        Guard l(_xNodeConnParam);
        return _mNodeConnParams.size();
    }
}

//-------------------------共识需要上面的接口（数据全部从共识中取） end--------------------------------------//
bool NodeConnParamsManager::nodeInfoHash(h256& h) 
{
        eth::NodeConnParams stNodeConnParam;
        std::string sNodeId = m_host->id().hex();
        if (!getNodeConnInfoBoth(sNodeId, stNodeConnParam))
        {
            LOG(INFO) << "No NodeConninfo (" << sNodeId << ")." ;
            return false;
        }
        //crypto Common中进行加密
        RLPStream _rlps;
    _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
    // _rlps.appendList(2) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo;//不使用ip进行数字签名操作
    h = dev::sha3(_rlps.out());
    LOG(INFO) << " getSelfNodeInfo hash is " << h.abridged() << "|nodeid is " << stNodeConnParam._sNodeId.substr(0, 6) << "|agenceinfo is " << stNodeConnParam._sAgencyInfo.substr(0, 6) << "|ip is " << stNodeConnParam._sIP ;
    return true;
}
//签名验证的数据全部从两边数据中读取
bool NodeConnParamsManager::signNodeInfo(CABaseData & caBaseData)
{
    try {
        // eth::NodeConnParams stNodeConnParam;
        // std::string sNodeId = m_host->id().hex();
        // if (!getNodeConnInfoBoth(sNodeId, stNodeConnParam))
        // {
        //     LOG(INFO) << "No NodeConninfo (" << sNodeId << ")." ;
        //     return false;
        // }
        // //crypto Common中进行加密
        // RLPStream _rlps;
        // _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
		// // _rlps.appendList(2) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo;//不使用ip进行数字签名操作
        // auto hash = dev::sha3(_rlps.out());
        // LOG(INFO) << " getSelfSignData hash is " << hash << "|nodeid is " << stNodeConnParam._sNodeId << "|agenceinfo is " << stNodeConnParam._sAgencyInfo << "|ip is " << stNodeConnParam._sIP ;
        h256 hash;
        if (!nodeInfoHash(hash))
            return false;

        caBaseData.setNodeSign(sign(m_host->sec(), hash));
        return true;
    }
    catch (...)
    {
        LOG(INFO) << "getSelfSignData throws exceptions. nodeId is " << m_host->id().hex() ;
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
        LOG(ERROR) << "catch exception in RSAKeySign" ;
    }

    return true;
}

bool NodeConnParamsManager::checkNodeInfo(const string& sEndPointNodeId, const h256& h) 
{
    bool bRet = false;
    LOG(INFO) << "checkNodeConnInfo nodeid is " << sEndPointNodeId.substr(0, 6) << "| nodeinfo hash is " << h.abridged();
    //获取对端节点信息（从合约还有配置文件中取）
    NodeConnParams stNodeConnParam;
    if (!getNodeConnInfoBoth(sEndPointNodeId, stNodeConnParam))
    {
        LOG(ERROR) << "No NodeConninfo (" << sEndPointNodeId.substr(0, 6) << ")." ;
        return bRet;
    }

    //对数据进行hash
    RLPStream _rlps;
    _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
    auto hash = dev::sha3(_rlps.out());

    LOG(INFO) << "checkNodeConnInfo(from local data) hash is " << hash.abridged() << "|" << stNodeConnParam._sNodeId.substr(0, 6) << "|" << stNodeConnParam._sAgencyInfo.substr(0, 6) << "|" << stNodeConnParam._sIP ;    
    //判断数据是否一致
    if (hash != h)
    {
        LOG(ERROR) << "NodeInfo hash error. (" << sEndPointNodeId.substr(0, 6) << ")'s hash is " << h.abridged() << "." ;
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
    //获取对端节点信息（从合约还有配置文件中取）
    NodeConnParams stNodeConnParam;
    if (!getNodeConnInfoBoth(sEndPointNodeId, stNodeConnParam))
    {
        LOG(ERROR) << "No NodeConninfo (" << sEndPointNodeId << ")." ;
        return bRet;
    }

    //对数据进行hash
    RLPStream _rlps;
    _rlps.appendList(3) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo << stNodeConnParam._sIP;
	// _rlps.appendList(2) << stNodeConnParam._sNodeId << stNodeConnParam._sAgencyInfo;//只对nodeid等信息进行签名，不对IP信息进行签名
    auto hash = dev::sha3(_rlps.out());

    LOG(INFO) << "checkNodeConnInfo hash is " << hash << "|" << stNodeConnParam._sNodeId << "|" << stNodeConnParam._sAgencyInfo << "|" << stNodeConnParam._sIP;
    //用公钥进行解析
    //判断数据是否一致
    if (!verify(Public(sEndPointNodeId), caBaseData.getNodeSign(), hash))
    {
        LOG(ERROR) << "Sign error. (" << sEndPointNodeId << ") sSign is " << caBaseData.getNodeSign() << "." ;
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
        //没有开启，统一返回true
        LOG(DEBUG) << "CAVerify is false." ;
        return true;
    }

    eth::CaInfo caInfo;
    std::shared_ptr<eth::SystemContractApi> pSysContractApi = getSysContractApi();
    pSysContractApi->getCaInfo(serialNumber, caInfo);

    LOG(DEBUG) << "serialNumber:" << serialNumber << "|cainfo:" << caInfo.toString();

    if (caInfo.blocknumber == 0 || caInfo.status != eth::CaStatus::Ok)
    {
        LOG(INFO) << "blockNumber is 0 or status is false." ;
        return false;
    }

    if (caInfo.hash != serialNumber)
    {
        LOG(DEBUG) << "certSerialNum is not in contract";
        return false;
    }

    /*if (!caInfo.black.empty())
    {
        for (std::string blackIp : caInfo.getBlackList())
        {
            if (blackIp == ip)
            {
                LOG(INFO) << "remoteIp is in black list.remoteIp:" << ip ;
                return false;
            }
        }
    }

    if (!caInfo.white.empty())
    {
        bool found = false;
        for (std::string whiteIp : caInfo.getWhiteList())
        {
            if (whiteIp == ip)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            LOG(INFO) << "remoteIp is not in white list.remoteIp:" << ip ;
            return false;
        }
    }*/
    return true;
}

// DEPRECATED
bool NodeConnParamsManager::checkCA(std::string remoteNodeID, CABaseData & caBaseData)
{
    bool caVerify = eth::NodeConnParamsManager::CAVerify;
    if (!caVerify)
    {
        //没有开启，统一返回true
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
bool NodeConnParamsManager::CheckAndSerialize(const RLP &_rlp, RLPBaseData &_rbd, CABaseData &caBaseData)
{
    Public pub = _rlp[4].toHash<Public>();//公钥hex编码
    Signature signature = _rlp[5].toHash<Signature>();       //获取签名信息
    std::string caSign = _rlp[6].toString();//ca签名数据
    std::string caPub256 = _rlp[7].toString();//公钥Hash
    LOG(INFO) << "SerializeHandShakeRLP,pub:" << pub << ",signature:" << signature << ",caPub256:" << caPub256 ;

    std::string seed(_rbd.getSeed().begin(), _rbd.getSeed().end());
    caBaseData.setSeed(seed);
    caBaseData.setNodeSign(signature);
    caBaseData.setSign(caSign);
    caBaseData.setPub256(caPub256);

    return CheckAll(pub.hex(), caBaseData);
    //checkNodeInfo(pub.hex(), wbCAData) && checkCA(pub.hex(), seedStr, wbCAData);
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
        LOG(ERROR) << "seedStr is empty.nodeId:" << m_host->id().hex() ;
        return;
    }

    WBCAData wbCAData;
    bool ok = signNodeInfo(wbCAData);
    if (!ok)
    {
        LOG(ERROR) << "signNodeInfo false!!! nodeId:" << m_host->id().hex() ;
        return;
    }
    ok = signCAInfo(seedStr, wbCAData);
    if (!ok)
    {
        LOG(ERROR) << "signCAInfo false!!! nodeId:" << m_host->id().hex() ;
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

//证书内容更新，或者吊销的
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

void NodeConnParamsManager::SetHost(Host *host)
{
    m_host = host;
};

