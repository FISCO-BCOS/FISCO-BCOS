/** 
FileInfoManager contract call
*/

#pragma once

#include <string>
#include <libethereum/Client.h>
#include "libcontract/BaseContract.h"

namespace contract
{

class FileInfoManagerContract : public BaseContract {
public:
	FileInfoManagerContract(dev::eth::Client * client, const dev::Address& contractAddr = Address());
	~FileInfoManagerContract();

	/**
	*@desc 分页查询组文件信息
	*/
	int pageByGroup(std::string _group, int _pageNo, std::string& result);

	/**
	*@desc 列出文件组
	*/
	int listFileinfoByGroup(const std::string& groupName, std::string& ret);

	/**
	*@desc 从FileInfoManager合约获取所有文件信息json结果
	*/
	int listFileinfo(std::string& ret);

	/**
	*@desc 从FileInfoManager合约获取指定文件信息json结果
	*/
	int findFileInfo(const std::string& _id, std::string& ret);

	/**
	*@desc 获取组pageSize
	*/
	int getGroupPageCount(const std::string& group, int& result);

	/**
	*@desc 获取pageSize
	*/
	int getPageCount(int& result);


};

}
