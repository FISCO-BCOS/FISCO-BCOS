/** 
FileServerManager contract call
*/

#pragma once

#include <string>
#include <libethereum/Client.h>
#include "libcontract/BaseContract.h"

using namespace contract;

namespace contract
{


class FileServerManagerContract : public BaseContract {
public:
	FileServerManagerContract(dev::eth::Client* client, const dev::Address& contractAddr = Address());
	~FileServerManagerContract();

	/** 
	*@desc 从FileInfoManager合约获取所有文件服务器信息json结果 
	*/
	int listFileServer(std::string& ret);


	/**
	*@desc 查询指定文件服务器信息
	*/
	int findServerInfo(const std::string& serverid, std::string& ret);

	/** 
	*@desc 查询服务节点是否开启
	*/
	int isServerEnable(const std::string& serverid, bool& enable);
	
};

}
