#pragma once

#include "PrecompiledContext.h"
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <libstorage/Storage.h>

namespace dev {

namespace precompiled {

class PrecompiledContextFactory: public std::enable_shared_from_this<PrecompiledContextFactory> {
public:
	typedef std::shared_ptr<PrecompiledContextFactory> Ptr;

	virtual ~PrecompiledContextFactory() {};

	virtual void initPrecompiledContext(BlockInfo blockInfo, PrecompiledContext::Ptr context);

	virtual void setStateStorage(dev::storage::Storage::Ptr stateStorage);
	//void setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer) { _channelRPCServer = channelRPCServer; }
	//void setAMOPDBTopic(const std::string &AMOPDBTopic) { _AMOPDBTopic = AMOPDBTopic; }
	//void setMaxRetry(int maxRetry) { _maxRetry = maxRetry; }

private:
	dev::storage::Storage::Ptr _stateStorage;
	//ChannelRPCServer::Ptr _channelRPCServer;
	//std::string _AMOPDBTopic;
	//int _
	//int _maxRetry = 0;
};

}

}
