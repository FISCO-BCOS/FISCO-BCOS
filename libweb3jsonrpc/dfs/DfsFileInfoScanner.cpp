#include "DfsFileInfoScanner.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "json/json.h"
#include "DfsContractCaller.h"
#include "DfsCommon.h"
#include "DfsMd5.h"
#include "DfsFileServer.h"
#include "DfsFileOperationManager.h"
#include "DfsConst.h"
#include "DfsDownloadClient.h"
#include "DfsCommonClient.h"
#include "DfsFileRecorder.h"
#include "libethereum/Client.h"



using namespace dev;
using namespace dev::eth;
using namespace dev::rpc::fs;

static Client* psClient = NULL;

DfsFileInfoScanner::DfsFileInfoScanner(): Worker("filesync", 0)
{
	m_BasePath = "";

	m_SrcNode = "";
	m_GroupId = "";
	m_SyncState = FileSyncState::SYNC_NOT_INITED;
	m_InitBlockNum = 0;

	struct timeval nowTime;
	gettimeofday(&nowTime, NULL);
	m_TimeLastCheck = nowTime.tv_sec * 1000000 + nowTime.tv_usec;
}

DfsFileInfoScanner::~DfsFileInfoScanner()
{
	stop();
}

//init
int DfsFileInfoScanner::init(const vector<string> &containers, void* client)
{
	m_BasePath = DfsFileServer::getInstance()->m_StoreRootPath;
	m_SrcNode = DfsFileServer::getInstance()->m_NodeId;
	m_GroupId = DfsFileServer::getInstance()->m_NodeGroup;
	m_Containers = containers;

	//get all file info from contract
	map<string, DfsFileInfo>	fileinfos;
	getContractFileInfos(fileinfos);

	//get all file info from local dfs
	map<string, DfsFileInfo>	localFileinfos;
	getLocalFileInfos(localFileinfos);

	//check all the file infos and create file operation task
	processFileinfoInit(localFileinfos, fileinfos);

	//start the running thread
	m_SyncState = FileSyncState::SYNC_WAIT_INIT;
	m_InitBlockNum = 0;
	psClient = (Client*)client;

	startWorking();

	dfs_debug << "DfsFileInfoScanner (file sync module) has been inited !\n";
	return 0;
}

/// Called after thread is started from startWorking().
void DfsFileInfoScanner::startedWorking()
{
	dfs_debug << "the file info sanner is started... running\n";
}
	
/// Called continuously following sleep for m_idleWaitMs.
void DfsFileInfoScanner::doWork()
{
	if (m_SyncState == FileSyncState::SYNC_INIT_OK)
	{
		doTask();
	}
	else
	{
		if (checkBlockInit())
		{
			dfs_debug << "init state is OK, start real file working !!";
			m_SyncState = FileSyncState::SYNC_INIT_OK;
			if (!DfsFileServer::getInstance()->checkServiceAvailable())
			{
				dfs_warn << "the file service of node: " << m_SrcNode << "is not open !";
			}
			else
			{
				dfs_debug << "the file service of node: " << m_SrcNode << "is open !";
			}
		}
		else
		{
			static int tag = 0;
			if (tag == 0)
				dfs_debug << "init state is NOT OK, waiting... !!";
				
			usleep(1000);//1ms
		}
	}
}
	
bool DfsFileInfoScanner::checkBlockInit()
{
	if (!psClient->checkWorking())
		return false;
	
	return true;
	//as there has empty block, the code block is commented now
	/*int blockNum = psClient->number();
	struct timeval nowTime;
	gettimeofday(&nowTime, NULL);

	int utimeNow = nowTime.tv_sec * 1000000 + nowTime.tv_usec;
	if (blockNum > 0 && blockNum == m_InitBlockNum && utimeNow > m_TimeLastCheck + 3000000)
		return true;

	if (blockNum > m_InitBlockNum)
	{
		m_InitBlockNum = blockNum;
		m_TimeLastCheck = utimeNow;
	}
	
	return false;*/
}

void DfsFileInfoScanner::stop()
{
	stopWorking();
}

//thread cycle
void DfsFileInfoScanner::doTask()
{
	//1. process all tasks for the first
	if (!DfsFileServer::getInstance()->checkServiceAvailable() || m_DownloadFileInfos.size() <= 0)
	{
		//dfs_debug << "**** no files to process ***\n";
		for (int i = 0 ; i < 5; ++i)//sleep (5s)
		{
			
			if (!shouldStop())
				usleep(1000000);//1s
			else
				return;
		}
	}

	//execute all the download tasks
	DfsHttpClient* pClient = new DfsDownloadClient();

	for(auto &file : m_DownloadFileInfos) 
	{
		if (m_SrcNode == file.second.src_node)
		{
			dfs_debug << "get a file download task, but the src_node is self ###****\n";
			continue;
		}

		if (shouldStop())
			return;

		JzRequestInfo objReq;
		objReq.method = JZ_HTTP_METHOD_GET;
		objReq.host = file.second.host;
		objReq.port = file.second.port;
		objReq.module = JZ_MODULE_FS;
		objReq.container_id = file.second.container;
		objReq.version = JZ_MODULE_VERSION_V1;
		objReq.file_id = file.second.id;
		objReq.filename = file.second.filename;
		objReq.filehash = file.second.filehash;
		objReq.user_data = NULL;
		objReq.curl = NULL;

		dfs_debug << "to down file: " << file.first.c_str() << ", from src_node: " << file.second.src_node.c_str() <<"\n";
		pClient->init(objReq);

		DfsFileTask objTaskStart = file.second;
		file.second.operation = DfsFileTask::DOWNLOAD_START;
		if (0 != DfsFileRecorder::writeRecord(file.second))
	    {
	    	dfs_warn << "write down_start file update record failed !!";
	    }

		pClient->request("");
		pClient->endRequest();

		dfs_debug << "down file done: " << file.first.c_str() << ", from src_node: " << file.second.src_node.c_str() <<"\n";
		file.second.operation = pClient->getErrorCode() == 0 ? DfsFileTask::DOWNLOAD : DfsFileTask::DOWNLOAD_FAIL;
		if (0 != DfsFileRecorder::writeRecord(file.second))
	    {
	    	dfs_warn << "write down file update record failed !!";
	    }
	}

	m_DownloadFileInfos.clear();
	delete pClient;

	//dfs_debug << "**** to fetch info and generateTasks ...";
	//2. get all file info from contract
	map<string, DfsFileInfo>	fileInfos;
	getContractFileInfos(fileInfos);

    if (shouldStop())
		return;
	//3. calculate the files info changes
	generateTasks(fileInfos);

}

int DfsFileInfoScanner::queryGroupFileInfo(const std::string& group, vector<string>& result)
{
	int pageSize = 0;
	if (0 != getGroupPageCount(group, pageSize))
	{
		dfs_debug << "getGroupPageCount failed !";
		return -1;
	}

	for (int i = 0; i < pageSize; ++i)
	{
		string json;
		if (0 != pageByGroup(group, i, json))
		{
			dfs_debug << "pageByGroup failed, in call !";
			return -1;
		}

		result.push_back(json);
	}

	return 0;
}

int DfsFileInfoScanner::getContractFileInfos(map<string, DfsFileInfo>& fileInfos)
{
	vector<string> vecFileInfos;
	if (0 != queryGroupFileInfo(m_GroupId, vecFileInfos))
	{
		dfs_warn << "*** listGroupFileInfo failed ***\n";
		return -1;
	}

	//parse the file info list
	//dfs_debug << "*** listByGroup(" << m_GroupId.data() << ") pages ==> "<< vecFileInfos.size() <<"\n";
	if(0 != parseFileInfoList(vecFileInfos, fileInfos))
	{
		dfs_warn << "parseFileInfoList faield, size: " << vecFileInfos.size() << "\n";
		return -1;
	}

	//dfs_debug << "**** fs has "<< (int)fileInfos.size() << " fileinfo objects for group: " << m_GroupId.data() << "\n";
	return 0;
}



int DfsFileInfoScanner::parseFileInfoList(const vector<string>& vecJson, map<string, DfsFileInfo>& fileInfos)
{
	

	/* {"ret":0, 
	"data":{";
        "total":0,"items":
        [{"id",
		"container":"files",
		"filename":"filename001",
		"updateTime":"147899899",
		"size":1,
		"file_hash":"afsfsd33wrsdf",
		"type":1,
		"priviliges":"rwx",
		"src_node":"node_id_001",
		"node_group":"group001",
		"info":""
		}]
        }
      }
    */

    for (auto && strjson : vecJson)
    {
    	Json::Reader reader;
		Json::Value root;
    	string strRet;
		if (strjson.size() <= 0 || !reader.parse(strjson, root))
		{
			dfs_debug << "parse json: " << strjson.c_str() << "failed\n";
			return -1;
		}

		if (root["ret"].isNull() || root["data"].isNull())
		{
			dfs_debug << "bad json: " << strjson.c_str() << "\n";
			return -1;
		}

		strRet = root["ret"].asString();
		Json::Value &data = root["data"];
		if (data["items"].isNull() || data["total"].isNull())
		{
			dfs_debug << "the data node is invalid \n";
			return -1;
		}

		Json::Value &items = data["items"];
		//parse items to fileinfos
		//MUST: id, filename, file_hash, src_node, node_group
		string id;
		string filename;
		string file_hash;
		string src_node;
		string node_group;
		int count = items.size();
		for(int i = 0; i < count; ++i) 
		{
			if (items[i]["id"].isNull() || 
				items[i]["filename"].isNull() ||
				items[i]["file_hash"].isNull() ||
				items[i]["src_node"].isNull() ||
				items[i]["node_group"].isNull())
			{
				dfs_debug << "bad item, not complete json cannot parse";
				return -1;
			}

			//id,container,filename,node_group,src_node
			DfsFileInfo objFileInfo;
			objFileInfo.id = items[i]["id"].asString();
			objFileInfo.container = items[i]["container"].asString();
			objFileInfo.src_node = items[i]["src_node"].asString();
			objFileInfo.filename = items[i]["filename"].asString();
			objFileInfo.node_group = items[i]["node_group"].asString();
			objFileInfo.filehash = items[i]["file_hash"].asString();

			fileInfos.insert(std::pair<string, DfsFileInfo>(objFileInfo.id, objFileInfo));
		}
    }
    
	return 0;
}

int DfsFileInfoScanner::getLocalFileInfos(map<string, DfsFileInfo>& fileInfos)
{
	//get all files by container ids
	for (auto & container : m_Containers)
	{
		//all full file path
		vector<string> fullFiles;

		string strBasePath = DfsFileServer::getInstance()->m_StoreRootPath;
		string strDir;
		createDirectoryByContainer(strBasePath, container, strDir);

		listAllFiles(strDir, fullFiles, false);
		dfs_debug << "*****#### the local fs has: " << (int)fullFiles.size() << "files\n";

		for (auto & file : fullFiles)
		{
			//fill the info: id,container,filename,node_group,src_node
			vector<string> vecStr;
			SplitString(file, '_', vecStr);//to-do: '_'转义需要做

			if (vecStr.size() == 2)//fileid0001_filename0001
			{
				DfsFileInfo objFileInfo;
				objFileInfo.id = vecStr[0];
				objFileInfo.filename = vecStr[1];
				objFileInfo.container = container;
				objFileInfo.node_group = DfsFileServer::getInstance()->m_NodeGroup;

				string strFile = strDir;
				strFile += "/";
				strFile += file;
				// string strMd5 = "";
				// if(md5(strFile, strMd5, objFileInfo.file_hash) != 0)
				// {
				// 	printf("the file: %s access error !\n", strFile.c_str());
				// 	return -1;
				// }

				fileInfos.insert(std::pair<string, DfsFileInfo>(objFileInfo.id, objFileInfo));
			}
		}
	}
	
	for (auto & f : fileInfos)
	{
		dfs_debug << "********** the local file: " << f.first.data() << "\n";
	}
	dfs_debug << "****** getLocalFileInfos done, total="<< (int)fileInfos.size() << "\n";
	return 0;
}

int DfsFileInfoScanner::md5(const string& filepath, string& md5)
{
	MD5_CTX ctx;
	ms_MD5_Init(&ctx);

	char szBuf[4096];
	int bytes = 0;
	FILE* fp = fopen(filepath.data(), "rb");
	if (!fp)
		return -1;

	md5.clear();
	while((bytes=fread(szBuf, 1, sizeof(szBuf), fp)) > 0)
	{
		ms_MD5_Update(&ctx, szBuf, bytes);
	}
	fclose(fp);

	unsigned char szResult[16] = {0};
    memset(szResult, 0, 16);
	ms_MD5_Final(szResult, &ctx);

	char szTmp[32];
	memset(szTmp, 0, sizeof(szTmp));
	for (size_t i = 0; i < 16; ++i)
	{
		snprintf(szTmp, sizeof(szTmp), "%02X", szResult[i]);
		md5.append(szTmp, strlen(szTmp));
	}

	return 0;
}

int DfsFileInfoScanner::processFileinfoInit(map<string, DfsFileInfo>& localFileInfos, \
	map<string, DfsFileInfo>& fileInfos)
{
	//create delete, modify tasks
	for (auto &info : localFileInfos)
	{
		//1. file not exists
		if (fileInfos.find(info.first) == fileInfos.end())//not exists, delete file
		{
			//it is delete file task
			//id, container, filename, owner, src_node, node_group, operation;//TaskOperation
			dfs_debug << "***** to delete a file id: %s" << info.first.data() << "\n";

			string strFilePath;
			DfsFileOperationManager::getInstance()->delFile(info.second, strFilePath);
			continue;
		}

		//2. file exist
		//if modify filename
		if (info.second.filename != fileInfos[info.first].filename)
		{
			dfs_debug << "***** to modify a file id: %s " << info.first.data() << "***** \n";
			DfsFileOperationManager::getInstance()->modifyFile(info.second);
			continue;
		}
	}

	//download task
	for(auto & file : fileInfos)
	{
		if (localFileInfos.find(file.first) == localFileInfos.end())
		{
			//检查文件是否存在
			string strDownDir;
			createDirectoryByContainer(m_BasePath, file.second.container, strDownDir);

			string strFullFile;
			string strFullFileBak;
			createDfsFileNames(strDownDir, file.second.id, file.second.filename, strFullFile, strFullFileBak);

			string strHost;
			int port = 0;
			if (!ChkFileExists(strFullFile) && validateSourceNode(file.second.src_node, strHost, port))
			{
				dfs_debug << "*** add a download task, fileid: " << file.first.data() << "src node: " << file.second.src_node.data() << "\n";
				DfsFileTask objTask;
				objTask.id = file.second.id;
				objTask.container = file.second.container;
				objTask.filename = file.second.filename;
				objTask.filehash = file.second.filehash;
				objTask.owner = file.second.owner;
				objTask.src_node = file.second.src_node;
				objTask.node_group = file.second.node_group;
				objTask.host = strHost;
				objTask.port = port;
				objTask.operation = DfsFileTask::DOWNLOAD;
				objTask.directory = strDownDir;
				m_DownloadFileInfos.insert(std::pair<string, DfsFileTask>(file.first, objTask));
			}
			else
			{
				dfs_debug << "*** no need to down file fileid: " << file.first.data() << "src node: " << file.second.src_node.data() << ", filepath: "<< strFullFile.data() << "\n";
			}
		}
	}

	m_CacheFileInfos = fileInfos;
	return 0;
}

int DfsFileInfoScanner::generateTasks(map<string, DfsFileInfo>& fileInfos)
{
	//m_FileInfoMap
	processFileinfoInit(m_CacheFileInfos, fileInfos);
	return 0;
}

bool DfsFileInfoScanner::validateSourceNode(const string& src_node, string& fileHost, int& filePort)
{
	//fetch src node info
	string fileSeverJson;
	if (0 != findServerInfo(src_node, fileSeverJson))
	{
		dfs_warn << "**** cannot retrieve the src node : " << src_node.data() <<"info\n";
		return false;
	}

	//parse the json
	dfs_debug << "find the src node: " << fileSeverJson;
	Json::Reader reader;
	Json::Value root;
	string strRet;    
	if (fileSeverJson.size() <= 0 || !reader.parse(fileSeverJson, root))
	{
		dfs_warn << "parse json: " << fileSeverJson.c_str() << " failed\n";
		return false;
	}

	if (root["ret"].isNull() || root["data"].isNull())
	{
		dfs_warn << "bad json: " << fileSeverJson.c_str() << " not complete\n";
		return false;
	}

	strRet = root["ret"].asString();
	Json::Value &data = root["data"];
	if (data["items"].isNull() || !data["items"].isArray())
	{
		dfs_warn << "the data node is invalid \n";
		return false;
	}

	Json::Value &items = data["items"];
	//parse items to fileinfos
	//MUST: id; host; port; updateTime; organization; position; group; info;
	string id;
	string group;
	string host;
	int port;
	int enable = 0;
	int count = items.size();
	if (count <= 0)
	{
		dfs_warn << "cannot find the src node: " << src_node.data() << "\n";
		return false;
	}

	const int i = 0;
	if (items[i]["id"].isNull() || 
			items[i]["host"].isNull() ||
			items[i]["port"].isNull() ||
			items[i]["group"].isNull() ||
			items[i]["enable"].isNull())
	{
		dfs_warn << "bad item, not complete json cannot parse";
		return false;
	}

	//id; host; port; updateTime; organization; position; group; info;
	id = items[i]["id"].asString();
	group = items[i]["group"].asString();
	port = items[i]["port"].asInt();
	host = items[i]["host"].asString();
	enable = items[i]["enable"].asInt(); 

	//check group
	if (group != m_GroupId)
	{
		dfs_warn << "the src node group in blockchains is : "<< group.data() << " current group: " << m_GroupId.data() << "\n";
		return false;
	}

	if (enable == 0)
	{
		dfs_warn << "the src node: " << id << " is not on service !";
		return false;
	}

	fileHost = host;
	filePort = port;
	return true;
}
