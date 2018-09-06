/*
 * CommonInitializer.cpp
 *
 *  Created on: 2018年5月16日
 *      Author: 65193
 */

#include "CommonInitializer.h"
#include <libdevcore/easylog.h>
#include <boost/algorithm/string/replace.hpp>
#include <libethcore/BlockHeader.h>

using namespace dev;
using namespace dev::eth;

void CommonInitializer::initConfig(const boost::property_tree::ptree &pt) {
	_dataPath = pt.get<std::string>("common.data_path", "");
	_logConfig = pt.get<std::string>("common.log_config", "${DATAPATH}/log.conf");
	_extHeader = pt.get<unsigned int>("common.ext_header", 0);
	BlockHeader::updateHeight = _extHeader;
	
	completePath(_logConfig);

	if(_dataPath.empty()) {
		LOG(ERROR) << "Data path unspecified";

		BOOST_THROW_EXCEPTION(DataPathUnspecified());
	}
}

std::string CommonInitializer::dataPath() {
	return _dataPath;
}

std::string CommonInitializer::logConfig() {
	return _logConfig;
}

void CommonInitializer::completePath(std::string &path) {
	boost::algorithm::replace_first(path, "${DATAPATH}", _dataPath + "/");
}
