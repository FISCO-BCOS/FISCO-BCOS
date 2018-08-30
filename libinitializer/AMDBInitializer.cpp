/*
 * AMDBInitializer.cpp
 *
 *  Created on: 2018年5月9日
 *      Author: mo nan
 */

#include "AMDBInitializer.h"

#include <libstorage/MemoryStateDBFactory.h>
#include <libstorage/DBFactoryPrecompiled.h>
#include <libprecompiled/StringFactoryPrecompiled.h>
#include <libstorage/AMOPStateStorage.h>
#include <libstorage/LevelDBStateStorage.h>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>

using namespace dev;
using namespace dev::eth;

void AMDBInitializer::initConfig(const boost::property_tree::ptree &pt) {
	try {
		_type = pt.get<std::string>("statedb.type", "leveldb");

		if(_type == "amop") {
			//读取amopdb的配置
			_amopDBConfig.topic = pt.get<std::string>("statedb.topic", "DB");
			_amopDBConfig.retryInterval = pt.get<int>("statedb.retryInterval", 1);
			_amopDBConfig.maxRetry = pt.get<int>("statedb.maxRetry", 0);

			dev::storage::AMOPStateStorage::Ptr stateStorage = std::make_shared<dev::storage::AMOPStateStorage>();
			stateStorage->setChannelRPCServer(_channelRPCServer);
			stateStorage->setTopic(_amopDBConfig.topic);
			stateStorage->setMaxRetry(_amopDBConfig.maxRetry);

			_stateStorage = stateStorage;

			dev::precompiled::PrecompiledContextFactory::Ptr precompiledContextFactory = std::make_shared<dev::precompiled::PrecompiledContextFactory>();
			precompiledContextFactory->setStateStorage(stateStorage);

			_precompiledContextFactory = precompiledContextFactory;
		}
		else if(_type == "leveldb") {
			//读取leveldb的配置
			_levelDBConfig.path = pt.get<std::string>("statedb.path", "${DATAPATH}/statedb");
			completePath(_levelDBConfig.path);

			boost::filesystem::create_directories(_levelDBConfig.path);

			leveldb::Options option;
			option.create_if_missing = true;
			option.max_open_files = 100;

			leveldb::DB *dbPtr = NULL;
			leveldb::Status s = leveldb::DB::Open(option, _levelDBConfig.path, &dbPtr);
			if(!s.ok()) {
				LOG(ERROR) << "Open leveldb error: " << s.ToString();

				BOOST_THROW_EXCEPTION(LevelDBError());
			}

			auto db = std::shared_ptr<leveldb::DB>(dbPtr);

			auto stateStorage = std::make_shared<dev::storage::LevelDBStateStorage>();
			stateStorage->setDB(db);

			_stateStorage = stateStorage;

			dev::precompiled::PrecompiledContextFactory::Ptr precompiledContextFactory = std::make_shared<dev::precompiled::PrecompiledContextFactory>();
			precompiledContextFactory->setStateStorage(stateStorage);

			_precompiledContextFactory = precompiledContextFactory;
		}
		else {
			//错误的类型
			LOG(ERROR) << "Unknown statedb type: " << _type;

			BOOST_THROW_EXCEPTION(UnknownStateDBType());
		}
	}
	catch(std::exception &e) {
		LOG(ERROR) << "Wrong amdb config: " << boost::diagnostic_information(e);

		BOOST_THROW_EXCEPTION(e);
	}
}

dev::storage::StateStorage::Ptr AMDBInitializer::stateStorage() {
	return _stateStorage;
}

void AMDBInitializer::setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer) {
	_channelRPCServer = channelRPCServer;
}

dev::precompiled::PrecompiledContextFactory::Ptr AMDBInitializer::precompiledContextFactory() {
	return _precompiledContextFactory;
}

void AMDBInitializer::setDataPath(std::string dataPath) {
	_dataPath = dataPath;
}

void AMDBInitializer::completePath(std::string &path) {
	boost::algorithm::replace_first(path, "${DATAPATH}", _dataPath + "/");
}
