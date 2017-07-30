#include "DfsFileRecorder.h"
#include "DfsCommon.h"
#include "DfsConst.h"
#include <sys/time.h>
#include "DfsBase.h"


using std::string;
using namespace dev::rpc::fs;


DfsFileRecorder::DfsFileRecorder()
{}


DfsFileRecorder::~DfsFileRecorder()
{}


static string convertOperation(int operation);

int DfsFileRecorder::writeRecord(const DfsFileTask& task)
{
	string strFile = task.directory;
	strFile += "/";
	strFile += JZ_FILE_RECORD_FILE;
	FILE* file = NULL;
	if (!ChkFileExists(strFile))
	{
		file = fopen(strFile.data(), "wb");
	}
	else
	{
		file = fopen(strFile.data(), "ab");
	}

	if (file == NULL)
	{
		dfs_warn << "**** error ==> open file: " << strFile.data();
		return -1;
	}

	struct timeval objTime;
    gettimeofday(&objTime, NULL);
	fprintf(file, "%s, %s, %s, %ld\n", task.id.data(), task.filename.data(), convertOperation(task.operation).data(), (objTime.tv_sec * 1000000 + objTime.tv_usec)/1000);
	fclose(file);
	return 0;
}


 static string convertOperation(int  operation)
{	
	/**
	enum TaskOperation
    {
        UPLOAD = 0,
        DELETE = 1,
        MODIFY = 2,
        DOWNLOAD = 3
    };
	*/
	string op = "UNKOWN";
	switch(operation)
	{
		case DfsFileTask::UPLOAD_START:
			op = "UPLOAD_START";
			break;
		case DfsFileTask::UPLOAD:
			op = "UPLOAD";
			break;
		case DfsFileTask::DELETE:
			op = "DELETE";
			break;
		case DfsFileTask::MODIFY:
			op = "MODIFY";
			break;
		case DfsFileTask::DOWNLOAD_START:
			op = "DOWNLOAD_START";
			break;
		case DfsFileTask::DOWNLOAD_FAIL:
			op = "DOWNLOAD_FAIL";
			break;
		case DfsFileTask::DOWNLOAD:
			op = "DOWNLOAD";
			break;
		default:
			break;
	}

	return op;
} 