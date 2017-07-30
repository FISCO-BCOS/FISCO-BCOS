#pragma once

#include <vector>
#include <map>
#include <pthread.h>

#include "DfsBase.h"

using std::string;


namespace dev
{

namespace rpc
{

namespace fs
{

class DfsFileOperationManager
{
public:
	DfsFileOperationManager();
	virtual ~DfsFileOperationManager();

	static DfsFileOperationManager* getInstance();


public:
	int delFile(const DfsFileInfo& fileinfo, string& deleteFilePath);

	int modifyFile(const DfsFileInfo& fileinfo);


private:
	bool findShortestAsFile(const string& dir, const string& file_id, string& full_file);

private:
	pthread_mutex_t				m_Mutex;
	//
	
};

}
}
}