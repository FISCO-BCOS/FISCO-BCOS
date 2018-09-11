/*
 * PBFTInitializer.h
 *
 *  Created on: 2018年5月9日
 *      Author: mo nan
 */
#pragma once

#include <libpbftseal/PBFT.h>
#include <boost/property_tree/ptree.hpp>

namespace dev {

namespace eth {

class PBFTInitializer: public std::enable_shared_from_this<PBFTInitializer> {
public:
	typedef std::shared_ptr<PBFTInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);

	void setStateStorage(dev::storage::Storage::Ptr stateStorage);

	PBFT::Ptr pbft();

private:
	PBFT::Ptr _pbft;
	dev::storage::Storage::Ptr _stateStorage;
};

}

}
