#include "DfsJsonUtils.h"
#include <stdio.h>
#include <string.h>


using namespace dev::rpc::fs;


DfsJsonUtils::DfsJsonUtils()
{}

DfsJsonUtils::~DfsJsonUtils()
{}


string DfsJsonUtils::createCommonRsp(int ret, int code, const string& info)
{
	char szTemp[1024] = {0};
	memset(szTemp, 0, 1024);

	snprintf(szTemp, sizeof(szTemp), "{\"ret\":%d,\"code\":%d,\"info\":\"%s\"}", ret, code, info.c_str());
	return szTemp;
}

string DfsJsonUtils::createCommonRsp(int ret, int code, const string& info, const string& hash)
{
	char szTemp[1024] = {0};
	memset(szTemp, 0, 1024);

	snprintf(szTemp, sizeof(szTemp), "{\"ret\":%d,\"code\":%d,\"hash\":\"%s\",\"info\":\"%s\"}", ret, code, hash.data(), info.c_str());
	return szTemp;
}
