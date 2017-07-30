#pragma once


#include <string>
#include <libethereum/Client.h>

using std::string;


namespace dev
{

namespace rpc
{

namespace fs
{



/**
*@desc initiate the caller 
*/
int init_caller_client(dev::eth::Client* client);

/**
*@desc list all the file info
*/
int listFileInfo(string& result);

/**
*@desc list all the file info by group
*/
int listGroupFileInfo(const string& group, string& result);

/**
*@desc find the specific file info by fileid
*/
int findFileInfo(const string& fileid, string& result);

/**
*@desc list all the file server info
*/
int listServerInfo(string& result);

/**
*@desc find the specific server info by serverid
*/
int findServerInfo(const string& serverid, string& result);

/**
*@desc add file info
*/
int insertFileInfo(const string& fileinfo);


/**
*@desc fetch file info by group and page number
*/
int pageByGroup(string _group, int _pageNo, string& result);

/**
*@desc get page count of a group file info
*/
int getGroupPageCount(const std::string& group, int& result);

/**
*@desc get file info page count
*/
int getPageCount(int& result);

/**
*@desc check if the service node is enabled
*/
int checkServiceNode(const std::string& nodeid, bool& enable);

}

}
}

