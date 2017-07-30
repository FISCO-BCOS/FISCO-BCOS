/**
*  @Filename DfsCommon.cpp
*  @Author huanggaofeng
*  @Date 2017-01-31
*  @brief utils functions
*/

#include "DfsCommon.h"
#include "DfsBase.h"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include "DfsMd5.h"


using namespace dev::rpc::fs;


namespace dev
{

namespace rpc
{

namespace fs
{


/// check if the specific directory exists
bool ChkDirExists(const string &pm_sDir)
{
    struct stat stDir;

    if (stat(pm_sDir.c_str(), &stDir) != 0)
    {
        return false;
    }

    return S_ISDIR(stDir.st_mode);
}

/// check if the specific directory has write permission
bool ChkDirWritePerm(const string &pm_sDir)
{
    return (0 == access(pm_sDir.data(), W_OK));
}

/// check if the specific file exists
bool ChkFileExists(const string &pm_sFile)
{
    struct stat stFile;

    if (stat(pm_sFile.c_str(), &stFile) != 0)
    {
        return false;
    }

    return S_ISREG(stFile.st_mode);
}

/// get the file size
long GetFileSize(const string &pm_sFile)
{
    struct stat stFile;

    if (stat(pm_sFile.c_str(), &stFile) != 0)
    {
        return -1;
    }

    return stFile.st_size;
}

/// simple copy file function
int FileCpy(const string &pm_sSrc, const string &pm_sDest)
{
    FILE* fp = NULL;
    FILE* fp_dst = NULL;
    fp = fopen(pm_sSrc.c_str(), "rb");
    if (fp == NULL)
    {
        return -1;
    }

    fp_dst = fopen(pm_sDest.c_str(), "wb");
    char szBuffer[4096];
    size_t bytes = 0;
    size_t bytes_write = 0;
    size_t bytes_read = 0;
    while((bytes=fread(szBuffer, 1, 4096, fp)) > 0) {
        bytes_read += bytes;
        int ret = fwrite(szBuffer, 1, bytes, fp_dst);
        if (ret)
        {
            bytes_write += ret;
        }
    }

    fclose(fp);
    fclose(fp_dst);
    if (bytes_write != bytes_read)
    {
        return -1;
    }

    return 0;
}

/// simply move file function
int FileMv(const string &pm_sSrc, const string &pm_sDest)
{
    if (rename(pm_sSrc.c_str(), pm_sDest.c_str()) != 0)
    {
        return  -1;
    }

    return  0;
}

/// remove a file
int FileRm(const string &pm_sFileName)
{
    if (unlink(pm_sFileName.c_str()) != 0)
    {
        return  -1;
    }

    return  0;
}

// create a specific directory
bool createFileDir(const string &dirPath)
{
    if (dirPath.size() <= 0)
        return false;

    if (0 != mkdir(dirPath.c_str(), 0755))
        return false;

    return true;
}

//get the date string with the pattern: YYYYMMDD_HHMMSS
string currentTimestamp()
{
    struct timeval objTime;
    gettimeofday(&objTime, NULL);

    char szTime[256];
    memset(szTime, 0, 256);
    snprintf(szTime, 256, "%ld", objTime.tv_sec);

    return szTime;
}

void createDfsFileNames(const string& path, const string &fileID, const string &fileName, string &filename, string &filename_bak)
{
    filename = path;
    filename += "/";
    filename += fileID;
    filename += "_";
    filename += fileName;

    filename_bak = filename;
    filename_bak += "_";
    filename_bak += currentTimestamp();
    //strFileBak += ".bak";
}

void listAllFiles(const std::string &dirPath, vector<string> &files, bool fullPath)
{
    if (!ChkDirExists(dirPath))
    {
        return;
    }

    files.clear();
    DIR           *d;
    struct dirent *dir;
    d = opendir(dirPath.data());
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            {
                continue;
            }

            if (!fullPath)
            {
                files.push_back(dir->d_name);
            }
            else
            {
                string strFile = dirPath;
                strFile += "/";
                strFile += dir->d_name;
                files.push_back(strFile);
            }
        }

        closedir(d);
    }

    return;
}


void SplitString(const string &pm_sExpression, char pm_cDelimiter, vector<string> &pm_vecArray)
{
    size_t iPosBegin = 0;
    size_t iPosEnd;
    while ((iPosEnd = pm_sExpression.find(pm_cDelimiter, iPosBegin)) != string::npos)
    {
        pm_vecArray.push_back(pm_sExpression.substr(iPosBegin, iPosEnd - iPosBegin));
        iPosBegin = iPosEnd + 1;
    }
    pm_vecArray.push_back(pm_sExpression.substr(iPosBegin));
}

// create the directory path by container
void createDirectoryByContainer(const string& base, const string &container, string &directory)
{
    directory.clear();
    if (container.empty())
    {
        dfs_warn << "the container is null \n";
        return;
    }

    MD5_CTX ctx;
    unsigned char szResult[16] = {0};
    memset(szResult, 0, 16);
    ms_MD5_Init(&ctx);
    ms_MD5_Update(&ctx, container.data(), container.size());
    ms_MD5_Final(szResult, &ctx);
    unsigned char lowPart = szResult[0];
    unsigned char hignPart = szResult[8];

    char szDirAppend[256] = {0};
    memset(szDirAppend, 0, 256);
    snprintf(szDirAppend, 256, "%02X/%02X/%s", lowPart, hignPart, container.data());

    directory = base;
    directory += "/";
    directory += szDirAppend;
}

}
}
}

