/*
 * PBFTInitializer.h
 *
 *  Created on: 2018年5月16日
 *      Author: mo nan
 */
#pragma once

#include <boost/property_tree/ptree.hpp>
#include <libdevcrypto/Common.h>

namespace dev {

namespace eth {

DEV_SIMPLE_EXCEPTION(DataPathUnspecified);

class CommonInitializer: public std::enable_shared_from_this<CommonInitializer> {
public:
	typedef std::shared_ptr<CommonInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);

	std::string dataPath();
	std::string logConfig();

	void completePath(std::string &path);
private:
	std::string _dataPath;
	std::string _logConfig;
};

}

}
