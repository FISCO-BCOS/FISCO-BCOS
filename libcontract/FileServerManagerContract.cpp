#include "FileServerManagerContract.h"



using namespace dev;
using namespace contract;


FileServerManagerContract::FileServerManagerContract(dev::eth::Client* client, const dev::Address& contractAddr) : BaseContract(client, dev::Address(contractAddr))
{

}

FileServerManagerContract::~FileServerManagerContract()
{
}



//从FileInfoManager合约获取所有文件服务器信息json结果
int FileServerManagerContract::listFileServer(std::string& ret)
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

/**
*@desc 查询指定文件服务器信息
*/
int FileServerManagerContract::findServerInfo(const std::string& serverid, std::string& ret)
{
	reset();

	this->setFunction("find(string)");
	this->addParam(serverid);
	const bytes& response = this->call();

	ret = eth::abiOut<std::string>(response);
	if (ret.empty())
	{
		return -1;
	}

	return 0;
}

/** 
*@desc 查询服务节点是否开启
*/
int FileServerManagerContract::isServerEnable(const std::string& serverid, bool& enable)
{
	reset();

	enable = false;
	this->setFunction("isServerEnable(string)");
	this->addParam(serverid);

	int result = 0;
	if (0 != this->call(result))
	{
		return -1;
	}

	enable = result != 0 ? true : false; 
	return 0;
}