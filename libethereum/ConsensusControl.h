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
 * @file: ConsensusControl.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include "Interface.h"

#include <libdevcrypto/Common.h>
#include <libdevcore/Common.h>
#include <libethcore/Common.h>

#include <unordered_map>
#include <string>

namespace dev
{

namespace eth
{
/*
 * This class is for the feature of ConsensusControl, Which can be considered as adding a new rule for current consensus(etc. PBFT, RAFT..).
 * ConsensusControl should be considered as an instance in whole fisco-bcos process, as a module for enhancing restrict for consensus.
 * In the rule, we treat the agency list and the count of every agency as the basic elements, compared to the node.
 * Different node could belong to the same agency, and the rule use agency list and counts as the input to judge true or false.
 */
class ConsensusControl 
{
private:
	ConsensusControl() { }
	ConsensusControl(ConsensusControl const&) = delete;
	ConsensusControl& operator=(ConsensusControl const&) = delete;
	~ConsensusControl() { }

public:
	static ConsensusControl& instance() 
	{
		static ConsensusControl ins;
		return ins;
	}
	// Due to we get agency whould cast resource, we should use cache in consensus phase.

	// the the relationship of node and agency from NodeConnParamsManager
	void resetNodeCache(const dev::eth::BlockNumber _blk_no = dev::eth::LatestBlock);
	// clear
	void clearAllCache();
	// clear the consensus info which is related to blockhash
	void clearBlockCache(const dev::h256 _block_hash, const dev::Public _id = dev::Public());
	// add consensus info which is related to blockhash
	void addAgencyCount(const dev::h256 _block_hash, const dev::Public _id);

	// control
	// call the rule in consensus phase
	bool callConsensus(std::shared_ptr<Interface> _client, const dev::h256 _block_hash) const;
	// call the rule in checkBlockSign  (called in blockchain() to verify block signs validity)
	bool callConsensusInCheck(std::shared_ptr<Interface> _client, const dev::h512s & _id_list, const dev::eth::BlockNumber _blk_no = dev::eth::LatestBlock) const;

private:
	// cache relationship between public id(key) and agnecy name
	std::unordered_map<dev::Public, std::string> m_node_agency_map;
	// cache consensus info, represent map<blockhash, pair<set<publicid>, map<agnecyname, count>>> 
	// set<publicid> use to filter redundant package, map<agnecyname, count> use to cache the params for "call the consensus rule"
	std::unordered_map<dev::h256, std::pair<unordered_set<dev::Public>, unordered_map<std::string, u256>>> m_cache;
};

}
}