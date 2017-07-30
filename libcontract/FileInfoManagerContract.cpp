#include "FileInfoManagerContract.h"


using namespace dev;
using namespace contract;


const static std::string sc_FileInfoManagerAddr = "0x1000000000000000000000000000000000000018";


FileInfoManagerContract::FileInfoManagerContract(dev::eth::Client * client, const dev::Address& contractAddr) : BaseContract(client, dev::Address(contractAddr))
{
}

FileInfoManagerContract::~FileInfoManagerContract()
{
}


/**
*@desc 分页查询组文件信息
*/
int FileInfoManagerContract::pageByGroup(std::string _group, int _pageNo, std::string& result)
{
	reset();
    
    this->setFunction("pageByGroup(string,uint256)");
    this->addParam(_group);
    this->addParam(_pageNo);

    const bytes& response = this->call();
    
   	result = eth::abiOut<std::string>(response);
   	if (result.empty())
   	{
   		return -1;
   	}

   	return 0;
}


int FileInfoManagerContract::listFileinfoByGroup(const std::string& groupName, std::string& ret)
{
	reset();

	this->setFunction("listByGroup(string)");
	this->addParam(groupName);

	const bytes& response = this->call();

	ret = eth::abiOut<std::string>(response);
	if (ret.empty())
	{
		return -1;
	}

	return 0;
}

//从FileInfoManager合约获取所有文件信息json结果
int FileInfoManagerContract::listFileinfo(std::string& ret)
{
	reset();

	this->setFunction("listAll()");
	const bytes& response = this->call();

	ret = eth::abiOut<std::string>(response);
	if (ret.empty())
	{
		return -1;
	}

	return 0;
}

//从FileInfoManager合约获取指定文件信息json结果
int FileInfoManagerContract::findFileInfo(const std::string& _id, std::string& ret)
{
	reset();

	this->setFunction("findFileInfo(string)");
	this->addParam(_id);

	const bytes& response = this->call();

	ret = eth::abiOut<std::string>(response);
	if (ret.empty())
	{
		return -1;
	}

	return 0;
}

/**
*@desc 获取组pageSize
*/
int FileInfoManagerContract::getGroupPageCount(const std::string& group, int& result)
{
	reset();

	this->setFunction("getGroupPageCount(string)");
	this->addParam(group);

	if (0 != this->call(result))
	{
		return -1;
	}

	return 0;
}

/**
*@desc 获取pageSize
*/
int FileInfoManagerContract::getPageCount(int& result)
{
	reset();

	this->setFunction("getPageCount()");

	if (0 != this->call(result))
	{
		return -1;
	}

	return 0;
}


