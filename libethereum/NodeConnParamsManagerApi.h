
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
 * @file: NodeConnParamsManagerApi.h
 * @author: toxotguo
 * 
 * @date: 2018
 */

#pragma once
#include <vector>
#include <boost/algorithm/string.hpp>
#include <libethcore/Common.h>
#include <libdevcore/Guards.h>
#include <libp2p/Common.h>
#include <libp2p/SessionCAData.h>
#include <libp2p/HandshakeCAData.h>
#include <libp2p/Host.h>
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

			//for SSL
			std::string  serial;  
			std::string name; 
			
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


		class EthereumHost;
		class SystemContractApi;

		
		class NodeConnParamsManagerApi
		{
		public:
			NodeConnParamsManagerApi(){}
			virtual ~NodeConnParamsManagerApi(){}
			//-------------------------for No SSL-------------------------
			virtual bool getNodeConnInfo(std::string const& sNodeID, NodeConnParams &retNode) const { return false; };
			virtual bool getNodeConnInfoBoth(std::string const& sNodeID, NodeConnParams &retNode) const { return false; };
			virtual void getAllNodeConnInfoContractAndConf(std::map<std::string, NodeConnParams> & mNodeConnParams) const { };
			virtual void getAllConfNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const { };

			//-------------------------for ALL -------------------------
			virtual void getAllNodeConnInfo(int, std::map<std::string, NodeConnParams> & mNodeConnParams) const {  };
			virtual void getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const { };
	
			//-------------------------for SSL-------------------------
			virtual void getAllNode(int, std::map<std::string, NodeParams> & mNodeParams) const { };
			virtual void getAllConnect( std::map<std::string, NodeIPEndpoint> & mConnectParams) const { };
			virtual void updateAllConnect( std::map<std::string, NodeIPEndpoint> & mConnectParams)  { };
			virtual void caModifyCallback(const std::string& pub256) { };
			virtual void SetHost(std::shared_ptr<p2p::HostApi>) { };
			virtual bool checkCertOut(const std::string& serialNumber) { return false;};
			virtual void connNode(const ConnectParams &param) { };

			virtual void setNetworkFace(NetworkFace *net) { m_pnetwork = net; }
			virtual void setEthereumHost(EthereumHost *host) { m_phost = host; }
			virtual void setSysContractApi(std::shared_ptr<SystemContractApi> sysContractApi)  { m_pContractApi = sysContractApi; }
			virtual std::shared_ptr<SystemContractApi> getSysContractApi() const{ return m_pContractApi; }
			virtual bool addNewNodeConnInfo(const std::string &){ return true; };
			virtual bool addNewNodeConnInfo(const NodeConnParams &){ return true; };
			virtual void delNodeConnInfo(const std::string &, bool &){};
			virtual void connNode(const NodeConnParams &){};
			virtual	void sendNodeInfoSync(const std::vector<NodeConnParams> &){}

			virtual	void sendDelNodeInfoSync(const std::string &){}
			virtual void setInitInfo(const eth::ChainParams & cp)
			{
				m_initnodes = cp._vInitIdentityNodes;
				m_chainParams = cp;
			}

			virtual void disconnNode(const std::string &){};
			virtual bool getPublicKey(u256 const&, Public &) const { return false; }
			virtual bool getIdx(p2p::NodeID const&, u256 &) const { return false; }
			virtual unsigned getMinerNum() const { return 0; }
			virtual bool getAccountType(p2p::NodeID const&, unsigned &) const { return false; }
			virtual unsigned getNodeNum() const{ return 0; }

			// DEPRECATED
			virtual bool CheckConnectCert(const std::string& serialNumber,const std::string& ip){ return false; };
			virtual bool CheckAll(const std::string& , CABaseData &) {return true;};
			virtual void ConstructHandShakeRLP(RLPStream &_rlp, RLPBaseData &_rbd) {};
			virtual void SaveCADataInSession(const std::string, CABaseData &) {};
			virtual bool nodeInfoHash(dev::h256& h) { return false; };
			virtual bool checkNodeInfo(const std::string& remoteNodeID, const dev::h256& h) { return false; };
		protected:
		
			EthereumHost *m_phost = nullptr;
			NetworkFace *m_pnetwork = nullptr;
			std::shared_ptr<SystemContractApi> m_pContractApi = nullptr;
	        std::vector<std::string> m_initnodes;
			eth::ChainParams m_chainParams;
		};
	}

	class NodeConnManagerSingleton{
	public:
		static dev::eth::NodeConnParamsManagerApi& GetInstance();
	};
}
