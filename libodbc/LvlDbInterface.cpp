#include "LvlDbInterface.h"
#include "LMysql.h"
#include "LOracle.h"
#include <json_spirit/JsonSpiritHeaders.h>
#include <libethcore/CommonJS.h>
#include <libdevcore/FileSystem.h>

namespace js = json_spirit;
using namespace std;
using namespace leveldb;

LvlDbInterface* LvlDbInterfaceFactory::create(int dbUseType)
{

	std::string dataJsStr = dev::contentsString(dev::getConfigPath());
	js::mValue val;
	js::read_string(dataJsStr, val);
	js::mObject obj = val.get_obj();
	js::mObject dbObj = obj["dbconf"].get_obj();
	js::mObject dbTypeObj;
	switch(dbUseType)
	{
	case DBUseType::stateType:
		{
			dbTypeObj = dbObj["stateDbConf"].get_obj();
		}
		break;
	case DBUseType::blockType:
		{
			dbTypeObj = dbObj["blockDbConf"].get_obj();
		}
		break;
	case DBUseType::extrasType:
		{
			dbTypeObj = dbObj["extrasDbConf"].get_obj();
		}
		break;
		default:
		{
			LOG(ERROR) << "wrong db use type :" << dbUseType << std::endl;
			exit(-1);
		}
	}

	std::string sDbInfo = dbTypeObj["dbInfo"].get_str();
	std::string sDbName = dbTypeObj["dbName"].get_str();
	std::string sTableName = dbTypeObj["tableName"].get_str();
	std::string sUserName = dbTypeObj["userName"].get_str();
	std::string sPwd = dbTypeObj["pwd"].get_str();
	int iDbEngineType = dbTypeObj["engineType"].get_int();
	if (iDbEngineType != DBEngineType::mysql && iDbEngineType != DBEngineType::oracle)
	{
		LOG(ERROR) << "Only Support mysql and oracle. Wrong db engine. "<< iDbEngineType << std::endl;
		exit(-1);
	}

	int	iCacheSize = dbTypeObj["cacheSize"].get_int();

	return create(sDbInfo, sDbName, sTableName, sUserName, sPwd, iDbEngineType, iCacheSize);
}

LvlDbInterface* LvlDbInterfaceFactory::create(const std::string &sDbInfo, const std::string &sDbName, const std::string &sTableName, const std::string &username, const std::string &pwd, int dbEngineType, int iCacheSize)
{

	LvlDbInterface *pLvlDb = nullptr;
	switch (dbEngineType)
	{
	case DBEngineType::mysql:
	{
		pLvlDb	= new LMysql(sDbInfo, sDbName, sTableName, username, pwd, iCacheSize);
	}
		break;
	case DBEngineType::oracle:
	{
		pLvlDb = new LOracle(sDbInfo, sDbName, sTableName, username, pwd, iCacheSize);
	}
		break;
	default:
		break;
	}
	return pLvlDb;
}