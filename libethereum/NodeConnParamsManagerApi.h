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
 * @file: NodeConnParamsManagerApi.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once
#include <vector>
#include <boost/algorithm/string.hpp>
#include <libethcore/Common.h>
#include <libdevcore/Guards.h>
#include <libp2p/Common.h>
#include <libp2p/SessionCAData.h>
#include <libp2p/HandshakeCAData.h>
#include "ChainParams.h"

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
			
			virtual bool getNodeConnInfo(std::string const& sNodeID, NodeConnParams &retNode) const;

			
			virtual bool getNodeConnInfoBoth(std::string const& sNodeID, NodeConnParams &retNode) const;
			
			virtual void getAllNodeConnInfo(int, std::map<std::string, NodeConnParams> & mNodeConnParams) const = 0;
	
			virtual void getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const;
			
			virtual void getAllConfNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const;

			
			virtual void getAllNodeConnInfoContractAndConf(std::map<std::string, NodeConnParams> & mNodeConnParams) const;

			
			virtual void setNetworkFace(NetworkFace *net) { _pNetwork = net; }
			
			virtual void setEthereumHost(EthereumHost *host) { _pHost = host; }
			
			virtual void setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi)  { _pSysContractApi = sysContractApi; }
			
			virtual std::shared_ptr<SystemContractApi> getSysContractApi() const{ return _pSysContractApi; }
			
			virtual bool addNewNodeConnInfo(const std::string &){ return true; };
			virtual bool addNewNodeConnInfo(const NodeConnParams &){ return true; };
			
			virtual void delNodeConnInfo(const std::string &, bool &){};
			
			virtual void connNode(const NodeConnParams &){};
			
			virtual	void sendNodeInfoSync(const std::vector<NodeConnParams> &){}
			
			virtual	void sendDelNodeInfoSync(const std::string &){}
		    virtual void setInitIdentityNodes(const eth::ChainParams & cp){
				_vInitIdentityNodes = cp._vInitIdentityNodes;
			}
			virtual void setChainParams(const eth::ChainParams & cp){
				_chainParams = cp;
			}
			
			virtual void disconnNode(const std::string &){};
			//virtual void getAllNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const = 0;
			virtual bool getPublicKey(u256 const&, Public &) const { return false; }
			virtual bool getIdx(p2p::NodeID const&, u256 &) const { return false; }
			virtual unsigned getMinerNum() const { return 0; }
			virtual bool getAccountType(p2p::NodeID const&, unsigned &) const { return false; }
			virtual unsigned getNodeNum() const{ return 0; }

			
			// DEPRECATED
			virtual bool CheckConnectCert(const std::string& serialNumber,const std::string& ip) = 0;
			virtual bool CheckAndSerialize(const RLP &_rlp, RLPBaseData &_rbd, CABaseData &caBaseData) = 0;
			virtual bool CheckAll(const std::string& , CABaseData &) {return true;};
			virtual void ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd) = 0;
			virtual void SaveCADataInSession(const std::string, CABaseData &) {};
			virtual void SetHost(Host*) {};
			
			virtual bool nodeInfoHash(dev::h256& h) = 0;
			virtual bool checkNodeInfo(const std::string& remoteNodeID, const dev::h256& h) = 0;
		protected:
			
			static Mutex _xNodeConnParam;
			
			mutable std::map<std::string, NodeConnParams>	_mNodeConnParams;

			
			static Mutex _xConfigNodeConnParam;
			mutable std::map<std::string, NodeConnParams> _mConfNodeConnParams;
			
			EthereumHost *_pHost = nullptr;
			
			NetworkFace *_pNetwork = nullptr;
			
			std::shared_ptr<SystemContractApi> _pSysContractApi = nullptr;
			
	        std::vector<std::string> _vInitIdentityNodes;
			eth::ChainParams _chainParams;
			NodeConnParamsManagerApi(){}
		};
	}

	class NodeConnManagerSingleton{
	public:
		static dev::eth::NodeConnParamsManagerApi& GetInstance();
	};
}
