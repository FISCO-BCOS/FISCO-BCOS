#pragma once

#include <map>
#include <string>
#include <vector>
#include <pthread.h>
#include "DfsBase.h"
#include <libdevcore/Worker.h>


using std::string;
using std::map;
using std::vector;



namespace dev
{

namespace rpc
{

namespace fs
{

enum class FileSyncState : short
{
	SYNC_NOT_INITED = 0,
	SYNC_WAIT_INIT = 1,
	SYNC_INIT_OK = 2
};

class DfsFileInfoScanner : public Worker
{
public:
	DfsFileInfoScanner();
	~DfsFileInfoScanner();


public:
	//init
	int init(const vector<string> &containers, void* client);

	//stop
	void stop();


protected:
	//thread every cycle
	void doTask();

	/// Called after thread is started from startWorking().
	virtual void startedWorking() override;
	
	/// Called continuously following sleep for m_idleWaitMs.
	virtual void doWork() override;


private:
	int getContractFileInfos(map<string, DfsFileInfo>& fileInfos);

	//int parseFileInfoList(const string& strjson, map<string, DfsFileInfo>& fileInfos);
	int parseFileInfoList(const vector<string>& vecJson, map<string, DfsFileInfo>& fileInfos);

	int getLocalFileInfos(map<string, DfsFileInfo>& fileInfos);

	int md5(const string& filepath, string& md5);

	int processFileinfoInit(map<string, DfsFileInfo>& localFileInfos, map<string, DfsFileInfo>& fileInfos);

	int generateTasks(map<string, DfsFileInfo>& fileInfos);

	bool validateSourceNode(const string& src_node, string& fileHost, int& filePort);

	bool checkBlockInit();

	int queryGroupFileInfo(const std::string& group, vector<string>& result);

	
private:
	FileSyncState 									m_SyncState;
	string 											m_BasePath;
	string 											m_SrcNode;
	string 											m_GroupId;

	vector<string>									m_Containers;

	pthread_mutex_t									m_Mutex;
	map<string, DfsFileTask>						m_DownloadFileInfos;
	map<string, DfsFileInfo>						m_CacheFileInfos;
	
	int 											m_InitBlockNum;
	int 											m_TimeLastCheck;

};

}

}
}