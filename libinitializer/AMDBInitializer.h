/*
 * AMDBInitializer.h
 *
 *  Created on: 2018年5月9日
 *      Author: mo nan
 */

#pragma once

#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <libprecompiled/PrecompiledContextFactory.h>
#include <boost/property_tree/ptree.hpp>
#include <libdevcore/Exceptions.h>

namespace dev {

namespace eth {

DEV_SIMPLE_EXCEPTION(LevelDBError);
DEV_SIMPLE_EXCEPTION(UnknownStateDBType);

class AMDBInitializer: public std::enable_shared_from_this<AMDBInitializer> {
public:
	typedef std::shared_ptr<AMDBInitializer> Ptr;

	void initConfig(const boost::property_tree::ptree &pt);
	dev::storage::Storage::Ptr stateStorage();

	void setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer);
	dev::precompiled::PrecompiledContextFactory::Ptr precompiledContextFactory();

	void setDataPath(std::string dataPath);

private:
	void completePath(std::string &path);

	std::string _type;

	//bdb配置，暂未实现
	struct BDBConfig {

	} _bdbConfig;

	//amopdb配置
	struct AMOPDBConfig {
		std::string topic; //请求AMDB使用的topic
		int retryInterval = 1; //重试间隔
		int maxRetry = 0; //最大重试次数
	} _amopDBConfig;

	struct LevelDBConfig {
		std::string path; //leveldb路径
	} _levelDBConfig;

	ChannelRPCServer::Ptr _channelRPCServer;
	dev::precompiled::PrecompiledContextFactory::Ptr _precompiledContextFactory;
	dev::storage::Storage::Ptr _stateStorage;
	std::string _dataPath;
};

}

}
