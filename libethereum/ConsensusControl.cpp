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
 * @file: ConsensusControl.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "ConsensusControl.h"
#include "Client.h"
#include <libdevcore/easylog.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/easylog.h>
#include <libethcore/Transaction.h>
#include <libethcore/CommonJS.h>
#include <libethereum/NodeConnParamsManagerApi.h>
#include <abi/SolidityCoder.h>
#include <abi/SolidityAbi.h>

using namespace libabi;
using namespace std;

namespace dev
{

namespace eth
{

static void nodeAgencyCache(dev::eth::BlockNumber const _blk_no, std::unordered_map<dev::Public, std::string>& _node_agency_map) 
{
	_node_agency_map.clear();
	std::map<std::string, NodeConnParams> all_node;
	int blk_no = _blk_no;
	NodeConnManagerSingleton::GetInstance().getAllNodeConnInfo(blk_no, all_node);
	for (auto iter = all_node.begin(); iter != all_node.end(); ++iter) 
	{
		if (iter->second._iIdentityType == EN_ACCOUNT_TYPE_MINER) 
			_node_agency_map.insert(make_pair(jsToPublic(toJS(iter->second._sNodeId)), iter->second._sAgencyInfo));
	}
}

static bool callConsensusControl(std::shared_ptr<Interface> _client, const unordered_map<std::string, u256>& _agency_cache, const dev::eth::BlockNumber _blk_no = LatestBlock) 
{
	std::shared_ptr<SystemContractApi> systemContract = _client->getSystemContract();
	Address addr = systemContract->getRoute("ConsensusControlMgr");
	if (addr == Address()) { return true; } // ConsensusControlMgr not set in systemcontract

	Address god_addr = systemContract->getGodAddress();

	try 
	{
		u256 gas = TransactionBase::maxGas;
		u256 value = 0;
		u256 gasPrice = 1 * ether;

		bytes getaddr = abiIn("getAddr()");
		ExecutionResult tmpret = _client->call(god_addr, value, addr, getaddr, gas, gasPrice, _blk_no); // call in clientbase, not in client
		Address control_addr = abiOut<Address>(tmpret.output);

		LOG(DEBUG) << "[ConsensusControl] call for control contract for blk:" << ((Client*)_client.get())->blockChain().number() << ", addr is:" << control_addr;

		if (control_addr == Address()) // not set or not open for ConsensusControl
			return true;

		string func = "control(bytes32[],uint256[])";
		
		Json::Value root;
		Json::Value agnecyList;
		Json::Value numList;
		auto it2 = _agency_cache.begin();
		for (int index = 0; it2 != _agency_cache.end(); it2++, index++) 
		{
			agnecyList[index] = it2->first;
			numList[index] = (int)it2->second;
		}
		root.append(agnecyList);
		root.append(numList);
		// LOG(DEBUG) << "[ConsensusControl] params string:" << root.toStyledString();
		//  abi serialize
		SolidityAbi abi;
		string strData = SolidityCoder::getInstance()->encode(abi.constructFromFuncSig(func), root);
	
		// call consensus control contract
		ExecutionResult ret = _client->call(god_addr, value, control_addr, dev::jsToBytes(strData), gas, gasPrice, _blk_no);
		bool result_flag = abiOut<bool>(ret.output);
		LOG(TRACE) << "[ConsensusControl] call contract, ret:" << result_flag;
		return result_flag;
	} 
	catch (std::exception& e) 
	{
		LOG(ERROR) << "[ConsensusControl] construct or call contract error!." << e.what();
		return false;
	}
	return false;
}

void ConsensusControl::resetNodeCache(const dev::eth::BlockNumber _blk_no)
{
	// reset
	nodeAgencyCache(_blk_no, this->m_node_agency_map);
}

void ConsensusControl::clearAllCache() 
{
	m_cache.clear();
}

void ConsensusControl::clearBlockCache(const dev::h256 _block_hash, const dev::Public _id) 
{
	auto it = m_cache.find(_block_hash);
	if (it == m_cache.end()) // not cache data for this block
		return;

	if (_id == dev::Public()) // clear all data for this hash
	{
		m_cache.erase(it);
		return;
	}
	else
	{
		auto& filter = it->second.first;
		auto& agency_cache = it->second.second;
		auto filter_it = filter.find(_id);
		if (filter_it != filter.end()) 
		{
			filter.erase(filter_it); // remove related public key(id)
			// reduce the count of agency, if it's zero, earse it
			auto agency_it = m_node_agency_map.find(_id);
			if (agency_it != m_node_agency_map.end())
			{
				auto agnecy_cache_it = agency_cache.find(agency_it->second); // agency_it->second = agencyName
				if (agnecy_cache_it != agency_cache.end()) 
					agnecy_cache_it->second--; // reduce related agency count
				if (agnecy_cache_it->second <= 0)  // if count is zero, earse it
					agency_cache.erase(agnecy_cache_it);
			}
			
		}
	}	
}

void ConsensusControl::addAgencyCount(const dev::h256 _block_hash, const dev::Public _id) 
{
	
	auto node_it = m_node_agency_map.find(_id);
	if (node_it == m_node_agency_map.end()) 
	{
		LOG(ERROR) << "[ConsensusControl]public id:" << _id << "| not in init list";
		return;
	}

	string agency = node_it->second;
	// LOG(DEBUG) << "[ConsensusControl] addAgencyCount _block_hash:" << _block_hash << "|agency:" << agency;
	auto it = m_cache.find(_block_hash);
	if (it == m_cache.end()) 
	{
		m_cache[_block_hash].first.insert(_id);
		m_cache[_block_hash].second.insert(make_pair(agency, 1));
		return;
	}

	auto& filter = it->second.first;
	auto& agnecy_cache = it->second.second;

	auto filter_it = filter.find(_id);  // filter
	if (filter_it == filter.end()) // new id 
	{
		filter.insert(_id); // set new id
		auto agency_it = agnecy_cache.find(agency);
		if (agency_it == agnecy_cache.end()) // new agency
			agnecy_cache.insert(make_pair(agency, 1));
		else // old agency add count
			agency_it->second++; 
	} 
	else 
	{
		// old id for same _block_hash
		LOG(WARNING) << "[ConsensusControl] for agency:" << agency << "|receive same pub id:" << _id;
	}
}

bool ConsensusControl::callConsensus(std::shared_ptr<Interface> _client, const dev::h256 _block_hash) const
{
	// LOG(DEBUG) << "[ConsensusControl] in callConsensus";
	auto it = m_cache.find(_block_hash);
	if (it == m_cache.end()) 
	{
		LOG(ERROR) << "[ConsensusControl] can't find blockchain hash in cache:" << _block_hash;
		return false;
	}
	// agencyCountCache
	return callConsensusControl(_client, it->second.second);
}

bool ConsensusControl::callConsensusInCheck(std::shared_ptr<Interface> _client, const dev::h512s & _id_list, const dev::eth::BlockNumber _blk_no) const
{
	// LOG(DEBUG) << "[ConsensusControl] in callConsensusInCheck";
	unordered_map<dev::Public, std::string> node_agency_map;
	unordered_map<std::string, u256> agnecy_cache;
	nodeAgencyCache(_blk_no, node_agency_map);

	for (auto& id : _id_list) 
	{
		auto node_it = node_agency_map.find(id);
		if (node_it == node_agency_map.end()) 
		{
			LOG(ERROR) << "[ConsensusControl]public id:" << id << "| not in init list";
			continue;
		}
		string agency = node_it->second;
		auto agency_it = agnecy_cache.find(agency);
		if (agency_it == agnecy_cache.end()) // new agency
			agnecy_cache.insert(make_pair(agency, 1));
		else // old agency add count
			agency_it->second++; 
	}

	return callConsensusControl(_client, agnecy_cache, _blk_no);
}

}
}