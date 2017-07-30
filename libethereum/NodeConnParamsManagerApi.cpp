#include "NodeConnParamsManagerApi.h"
#include "NodeConnParamsManager.h"
#include <libdevcore/FileSystem.h>

using namespace std;
using namespace dev;
using namespace eth;

Mutex NodeConnParamsManagerApi::_xNodeConnParam;
Mutex NodeConnParamsManagerApi::_xConfigNodeConnParam;

bool NodeConnParamsManagerApi::getNodeConnInfo(std::string const& sNodeID, NodeConnParams &retNode) const
{
	bool bFind = false;
	Guard l(_xNodeConnParam);
	if (_mNodeConnParams.find(sNodeID) != _mNodeConnParams.end())
	{
		bFind = true;
		retNode = _mNodeConnParams[sNodeID];
	}
	return bFind;
}

//从内存中 和config数据中查找 更多的是用于自己的
bool NodeConnParamsManagerApi::getNodeConnInfoBoth(std::string const& sNodeID, NodeConnParams &retNode) const
{
	bool bFind = getNodeConnInfo(sNodeID, retNode);
	if (!bFind)
	{
		Guard l(_xConfigNodeConnParam);
		if (_mConfNodeConnParams.find(sNodeID) != _mConfNodeConnParams.end())
		{
			bFind = true;
			retNode = _mConfNodeConnParams[sNodeID];
		}
	}
	return bFind;
}


void NodeConnParamsManagerApi::getAllNodeConnInfoContract(std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
	Guard l(_xNodeConnParam);
	mNodeConnParams = _mNodeConnParams;
}


void NodeConnParamsManagerApi::getAllConfNodeConnInfo(std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
	Guard l(_xConfigNodeConnParam);
	mNodeConnParams = _mConfNodeConnParams;
}

void NodeConnParamsManagerApi::getAllNodeConnInfoContractAndConf(std::map<std::string, NodeConnParams> & mNodeConnParams) const
{
	mNodeConnParams.clear();
	{
		Guard l(_xNodeConnParam); 
		mNodeConnParams = _mNodeConnParams;
	}

	{
		Guard l(_xConfigNodeConnParam);
		for (auto node : _mConfNodeConnParams)
		{
			if (mNodeConnParams.find(node.first) == mNodeConnParams.end())
			{
				mNodeConnParams[node.first] = node.second;
			}
		}
	}

}

dev::eth::NodeConnParamsManagerApi& NodeConnManagerSingleton::GetInstance()
{
	if (getCaInitType() == "webank")
	{
		static dev::eth::NodeConnParamsManager nodeConnParamsManager(contentsString(getConfigPath()));

		return nodeConnParamsManager;
	}
	else{
		
		static dev::eth::NodeConnParamsManager nodeConnParamsManager(contentsString(getConfigPath()));

		return nodeConnParamsManager;
	}
}
