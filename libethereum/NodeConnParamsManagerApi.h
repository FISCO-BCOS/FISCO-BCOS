#pragma once
#include <vector>
#include <boost/algorithm/string.hpp>
#include <libethcore/Common.h>
#include <libdevcore/Guards.h>
#include <libp2p/Common.h>
#include <libp2p/SessionCAData.h>
#include <libp2p/HandshakeCAData.h>
#include "ChainParams.h"
#include <libdevcore/easylog.h>


using namespace dev::p2p;
enum AccountType {
	EN_ACCOUNT_TYPE_NORMAL = 0,
	EN_ACCOUNT_TYPE_MINER = 1
};
namespace dev
{
	class NetworkFace;
	namespace eth
	{
		class EthereumHost;
		class SystemContractApi;

		
		class NodeConnParamsManagerApi
		{
		public:
			~NodeConnParamsManagerApi(){}
			/**
			* 根据节点nodeid 获取对应的配置
			* in - sNodeID  节点的nodeid
			* out - retNode	节点的配置信息
			* out - bool	是否存在
			**/
			virtual bool getNodeConnInfo(std::string const& sNodeID, NodeConnParams &retNode) const;

			/**
			* 根据节点nodeid 获取对应的配置（从合约中读取，读取不到再从内存中读取， 主要用于查找自己的数据）
			* in - sNodeID  节点的nodeid
			* out - retNode	节点的配置信息
			* out - bool	是否存在
			**/
			virtual bool getNodeConnInfoBoth(std::string const& sNodeID, NodeConnParams &retNode) const;
			/**
			* 获取全部节点连接配置
			* int - blockNum	小于0为取当前配置 其余为对应区块的临时数据
			* out - mNodeConnParams 所有的节点配置信息
			**/
			virtual void getAllNodeConnInfo(int, std::map<std::string, NodeConnParams> & mNodeConnParams) const = 0;
		//	virtual void getAllNodeConnInfo(int, std::map<std::string, NodeConnParams> & mNodeConnParams) const{ getAllNodeConnInfoContractAndConf(mNodeConnParams); };
			/**
			* 获取全部节点连接配置
			* out - mNodeConnParams 所有的节点配置信息
			**/
			virtual void getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const;
			/**
			* 获取全部节点连接配置 config.json
			* out - mNodeConnParams 所有的节点配置信息
			**/
			virtual void getAllConfNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const;

			/**
			* 获取全部节点信息（合约和配置文件做并集，已合约的信息为主，即一个nodeid两边都有的话 已合约里对应的信息为主）
			* out - mNodeConnParams 所有的节点配置信息
			**/
			virtual void getAllNodeConnInfoContractAndConf(std::map<std::string, NodeConnParams> & mNodeConnParams) const;

			
			//设置NetworkFace
			//用于发送p2p新协议，
			virtual void setNetworkFace(NetworkFace *net) { _pNetwork = net; }
			//设置ethereumHost
			virtual void setEthereumHost(EthereumHost *host) { _pHost = host; }
			//设置sysEthereumApi  同时设置回调函数
			virtual void setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi)  { _pSysContractApi = sysContractApi; }
			//获取sysContractApi
			virtual std::shared_ptr<SystemContractApi> getSysContractApi() const{ return _pSysContractApi; }
			//增加新节点
			//in - sNewNodeConnInfo json结构的数据
			//out - 是否新加成功
			virtual bool addNewNodeConnInfo(const std::string &){ return true; };
			virtual bool addNewNodeConnInfo(const NodeConnParams &){ return true; };
			/**
			* 删除节点
			* in - sNodeId	需要删除的节点nodeid
			* out - bExisted	需要删除的节点是之前存在
			**/
			virtual void delNodeConnInfo(const std::string &, bool &){};
			/**
			* 对节点进行连接
			* in - param	需连接的节点
			**/
			virtual void connNode(const NodeConnParams &){};
			/**
			* 发起网络同步（增加的节点配置) 调用networkFace的发起网络请求
			* in - vParams	新加的节点连接
			**/
			virtual	void sendNodeInfoSync(const std::vector<NodeConnParams> &){}
			/**
			* 发起网络同步(删除某个节点信息) 调用networkFace的发起网络请求
			* in - sNodeId	需要删除的节点nodeid
			**/
			virtual	void sendDelNodeInfoSync(const std::string &){}
		    virtual void setInitIdentityNodes(const eth::ChainParams & cp){
				_vInitIdentityNodes = cp._vInitIdentityNodes;
			}
			virtual void setChainParams(const eth::ChainParams & cp){
				_chainParams = cp;
			}
			/**
			* 对已连接的节点进行断开
			* in - sNodeId	已连接节点的nodeid
			**/
			virtual void disconnNode(const std::string &){};
			//virtual void getAllNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const = 0;
			virtual bool getPublicKey(u256 const&, Public &) const { return false; }
			virtual bool getIdx(p2p::NodeID const&, u256 &) const { return false; }
			virtual unsigned getMinerNum() const { return 0; }
			virtual bool getAccountType(p2p::NodeID const&, unsigned &) const { return false; }
			virtual unsigned getNodeNum() const{ return 0; }

			/*
             *对连接控制，CA的一些连接控制
             **/
			virtual bool CheckAndSerialize(const RLP &_rlp, RLPBaseData &_rbd, CABaseData &caBaseData) = 0;
			virtual bool CheckAll(const std::string& , CABaseData &) {return true;};
			virtual void ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd) = 0;
			virtual void SaveCADataInSession(const std::string, CABaseData &) {};
			virtual void SetHost(Host*) {};
		protected:
			//节点信息锁
			static Mutex _xNodeConnParam;
			//节点连接信息(合约数据)
			mutable std::map<std::string, NodeConnParams>	_mNodeConnParams;

			//config.json中的配置数据 找自己信息的时候先找合约 找不到再从这里面找
			static Mutex _xConfigNodeConnParam;
			mutable std::map<std::string, NodeConnParams> _mConfNodeConnParams;
			//进行网络请求
			EthereumHost *_pHost = nullptr;
			//进行连接节点控制
			NetworkFace *_pNetwork = nullptr;
			//全局函数
			std::shared_ptr<SystemContractApi> _pSysContractApi = nullptr;
			////初始化时参与签名的节点数据
	        std::vector<std::string> _vInitIdentityNodes;
			eth::ChainParams _chainParams;
			NodeConnParamsManagerApi(){}
		};
	}

	class NodeConnManagerSingleton{
	public:
		//默认初始化生成webank的基类，则需要有webank的配置项。
		static dev::eth::NodeConnParamsManagerApi& GetInstance();
	};
}
