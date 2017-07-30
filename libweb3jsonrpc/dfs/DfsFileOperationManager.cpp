#include "DfsFileOperationManager.h"
#include "DfsCommon.h"
#include "DfsFileServer.h"
#include "DfsConst.h"
#include "string.h"


using namespace dev::rpc::fs;



DfsFileOperationManager::DfsFileOperationManager()
{
	pthread_mutexattr_t Attr;

	pthread_mutexattr_init(&Attr);
	pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_Mutex, &Attr);
}

DfsFileOperationManager::~DfsFileOperationManager()
{
	pthread_mutex_destroy(&m_Mutex);
}

DfsFileOperationManager* DfsFileOperationManager::getInstance()
{
	static DfsFileOperationManager sInstance;
	return &sInstance;
}

int DfsFileOperationManager::delFile(const DfsFileInfo& fileinfo, string& deleteFilePath)
{
	string strBasePath = DfsFileServer::getInstance()->m_StoreRootPath;
	string strDir;
	createDirectoryByContainer(strBasePath, fileinfo.container, strDir);

	//find the file shortest
	if(!findShortestAsFile(strDir, fileinfo.id, deleteFilePath))
	{
		dfs_warn << "cannot find the file_id: " << fileinfo.id.c_str() << "\n";
		return -1;
	}
	
	string strBakFile = deleteFilePath;
	strBakFile += "_";
	strBakFile += currentTimestamp();

	pthread_mutex_lock(&m_Mutex);
	//rename the file
	FileMv(deleteFilePath, strBakFile);
	pthread_mutex_unlock(&m_Mutex);

	dfs_debug << "delete file, use rename file " << deleteFilePath.data() << " to " << strBakFile.data() << "\n";
	return 0;
}


int DfsFileOperationManager::modifyFile(const DfsFileInfo& fileinfo)
{
	string strBasePath = DfsFileServer::getInstance()->m_StoreRootPath;
	string strDir;
	createDirectoryByContainer(strBasePath, fileinfo.container, strDir);

	//find the file shortest
	string strFullFileFound;
	if(!findShortestAsFile(strDir, fileinfo.id, strFullFileFound))
	{
		dfs_warn << "cannot find the file_id:  "<< fileinfo.id.c_str();
		return -1;
	}

	string strNewFile = strDir;
	strNewFile += "/";
	char szFile[256];
	memset(szFile, 0, 256);
	snprintf(szFile, sizeof(szFile), "%s_%s", fileinfo.id.c_str(), fileinfo.filename.c_str());
	strNewFile += szFile;

	pthread_mutex_lock(&m_Mutex);
	//rename the file
	FileMv(strFullFileFound, strNewFile);
	pthread_mutex_unlock(&m_Mutex);

	return 0;
}

bool DfsFileOperationManager::findShortestAsFile(const string& dir, const string& file_id, string& full_file)
{
	//find the file
	vector<string> files;
	vector<string> filesFound;
	listAllFiles(dir, files);
	for (std::vector<string>::iterator file = files.begin(); file != files.end(); ++file)
	{
		size_t pos = file->find(file_id);
		if (pos != string::npos && pos == 0)//begin with fileid
		{
			filesFound.push_back(*file);
		}
	}

	if (filesFound.empty())
	{
		dfs_warn << "file id: "<< file_id.c_str() << "not found\n";
		return false;
	}

	string strFileFound = filesFound.front();
	size_t length = filesFound.front().size();
	for (std::vector<string>::iterator f = filesFound.begin(); f != filesFound.end(); ++f)
	{
		if (length > f->size())//get the shortest one
		{
			length = f->size();
			strFileFound = *f;
		}
	}

	vector<string> vecStr;
	SplitString(strFileFound, '_', vecStr);
	if (vecStr.size() != 2)
	{
		dfs_warn << "cannot find the existing file, shortest file: "<<dir.data()<<"/" <<strFileFound.data() << "\n";
		return false;
	}

	full_file = dir;
	full_file += "/";
	full_file += strFileFound;
	return true;
}
