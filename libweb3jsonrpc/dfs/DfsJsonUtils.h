#pragma once

#include <json/json.h>
#include <string.h>

using namespace std;

namespace dev
{

namespace rpc
{

namespace fs
{

class DfsJsonUtils
{
public:
	DfsJsonUtils();
	~DfsJsonUtils();


public:
	static string createCommonRsp(int ret, int code, const string& info);
	static string createCommonRsp(int ret, int code, const string& info, const string& hash);
};

}
}
}

