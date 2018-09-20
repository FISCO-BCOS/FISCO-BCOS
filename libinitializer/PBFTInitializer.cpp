/*
 * PBFTInitializer.cpp
 *
 *  Created on: 2018年5月9日
 *      Author: mo nan
 */

#include "PBFTInitializer.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include <libpbftseal/PBFT.h>

using namespace dev;
using namespace dev::eth;

void PBFTInitializer::initConfig(const boost::property_tree::ptree &pt) {
	int blockInterval = pt.get<int>("pbft.block_interval", 1000);
	int allow_empty_block= pt.get<int>("pbft.allow_empty_block", 0);

	h512s minerList;
	for(auto it: pt.get_child("pbft")) {
		if(it.first.find("miner.") == 0) {
			LOG(DEBUG) << "Add " << it.first << " : " << it.second.data();

			h512 miner(it.second.data());
			minerList.push_back(miner);
		}
	}

	_pbft = std::make_shared<PBFT>();
	_pbft->setIntervalBlockTime(u256(blockInterval));
	_pbft->setOmitEmptyBlock(!allow_empty_block);
	_pbft->setMinerNodeList(minerList);
	_pbft->setStorage(_stateStorage);

	_pbft->init();
}

void PBFTInitializer::setStateStorage(dev::storage::Storage::Ptr stateStorage) {
	_stateStorage = stateStorage;
}

PBFT::Ptr PBFTInitializer::pbft() {
	return _pbft;
}
