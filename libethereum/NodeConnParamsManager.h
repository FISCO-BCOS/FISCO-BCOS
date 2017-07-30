#pragma once

#include <libdevcore/FileSystem.h>
#include "NodeConnParamsManagerApi.h"

namespace dev
{
class NetworkFace;

namespace eth
{

struct NodeConnManageNote : public LogChannel { static const char* name(){return "NodeConnManageNote";}; static const int verbosity = 3; };

class EthereumHost;
class SystemContractApi;

enum class CaStatus {
	Invalid,    //失效
	Ok       //有效
};
struct CaInfo
{
	std::string  hash;  // 节点机构证书哈希
	std::string pubkey;// 公钥
	std::string orgname;  // 机构名称
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


/***
* 节点配置管理类  单例
* 里面需要赋值NetworkFace和EthereumHost 指针进行控制
**/
class NodeConnParamsManager : public NodeConnParamsManagerApi
{
public:

	static bool CAVerify;

	NodeConnParamsManager(std::string const& _json);

	//增加新节点
	//in - sNewNodeConnInfo json结构的数据
	//out - 是否新加成功
	virtual bool addNewNodeConnInfo(const std::string &sNewNodeConnInfo) override;
	//in - nodeParam 配置节点的NodeConnParams数据结构
	//out - 是否新加成功
	virtual bool addNewNodeConnInfo(const NodeConnParams &nodeParam) override;
	/**
	* 删除节点
	* in - sNodeId	需要删除的节点nodeid
	* out - bExisted	需要删除的节点是之前存在
	**/
	virtual void delNodeConnInfo(const std::string &sNodeId, bool &bExisted) override;

	/**
	* 发起网络同步（增加的节点配置) 调用networkFace的发起网络请求
	* in - vParams	新加的节点连接
	**/
	virtual	void sendNodeInfoSync(const std::vector<NodeConnParams> &vParams) override;

	/**
	* 发起网络同步(删除某个节点信息) 调用networkFace的发起网络请求
	* in - sNodeId	需要删除的节点nodeid
	**/
	virtual	void sendDelNodeInfoSync(const std::string &sNodeId) override;

	//获取共识需要的数据
	virtual void getAllNodeConnInfo(int blockNum, std::map<std::string, NodeConnParams> & mNodeConnParams) const override;
	/**
	* 对节点进行连接
	* in - param	需连接的节点
	**/
	virtual void connNode(const NodeConnParams &param) override;

	/**
	* 对已连接的节点进行断开
	* in - sNodeId	已连接节点的nodeid
	**/
	virtual void disconnNode(const std::string & sNodeId) override;

	//设置sysEthereumApi  同时设置回调函数
	virtual void setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi) override;

	//将内存中的配置文件 刷入到文件中 ${datadir}/nodeextrainfo.json
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
	bool CheckAndSerialize(const RLP &_rlp, RLPBaseData &_rbd, CABaseData &caBaseData) override;
	bool CheckAll(const std::string& sNodeId, CABaseData &caBaseData) override ;
	void ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd) override ;
	void SaveCADataInSession(const std::string nodeId, CABaseData &caBaseData) override ;
	void CAInfoModifyCallback(const std::string& pub256);
	void CAVerifyModifyCallback();
	void SetHost(Host *host);

private:
	bool checkNodesValid(const std::vector< NodeConnParams> &vNodes);
	bool diffNodes(const std::vector< NodeConnParams> &vNodeParams, std::vector< NodeConnParams> &vAddNodes, std::vector< NodeConnParams> &vDelNodes);
	bool signNodeInfo(CABaseData & caBaseData);
	bool signCAInfo(std::string seedStr, CABaseData & caBaseData);
	bool checkNodeInfo(std::string remoteNodeID, CABaseData & caBaseData);
	bool checkIP(std::string remoteNodeID, CaInfo &caInfo);
	bool checkCA(std::string remoteNodeID, CABaseData & caBaseData);
	std::pair<bool, std::map<std::string, NodeConnParams> > getGodMiner(int  blockNum)const;
	
	Host* m_host;
};

}
}

