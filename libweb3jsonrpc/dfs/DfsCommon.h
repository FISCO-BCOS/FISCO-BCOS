/**
*  @Filename Common.h
*  @Date 2017-01-31
*  @brief utils functions
*/
#pragma once

#ifndef	_COMMON_H
#define _COMMON_H

#include <string>
#include <vector>

using std::string;
using std::vector;


#ifdef DEBUG
	#define DBMSG(x) x
#else
	#define DBMSG(x)
#endif


// RET
const int RET_RANGE	= -2;
const int RET_SYN	= -3;

const int DB_RETRY_TIME		= 3;

/// CODE
const int DB_SUCCESS		= 0;
const int DB_CONNECT_ERR	= -1000;
const int ATTACH_CONN_ERR	= -1001;
const int DB_NO_RECORD		= -1002;
const int RET_DB_ERR 		= -1003;

//// ERROR
const int DEFAULT_CODE		= 0;
const int CFGFILE_ERR		= -1;
const int DBPASS_ERR		= -2;
const int APP_ERR			= -3;
const int INPUT_PARAM_ERR	= -4;
const int RET_DATA_ERR		= -5;
const int RET_ABORT			= -6;
const int IO_ERR			= -7;
const int TIME_ERR			= -8;
const int FILE_DUP			= -9;

namespace dev
{

namespace rpc
{

namespace fs
{

/**
 *@brief  check if the directory exists
 *@param  pm_sDir     Ŀ¼
 *@return true/false
 */
bool ChkDirExists(const string &pm_sDir);

/**
 *@brief  check if write is allowed with the the directory
 *@param  pm_sDir     Ŀ¼
 *@return true/false
 */
bool ChkDirWritePerm(const string &pm_sDir);

/**
 *@brief  check if the specific file exists
 *@param  pm_sFile   the file
 *@return true/false
 */
bool ChkFileExists(const string &pm_sFile);

/**
 *@brief  get the content size of a file
 *@param  pm_sFile   the file
 *@return the size of a file
 */
long GetFileSize(const string &pm_sFile);

/**
 *@brief  copy a file
 *@param  pm_sSrc        source file
 *@param  pm_sDest       destination file
 *@return -1        fail
 *        0         success
 */
int FileCpy(const string &pm_sSrc, const string &pm_sDest);

/**
 *@brief  move a file
 *@param  pm_sSrc        source file
 *@param  pm_sDest       destination file
 *@return -1        fail
 *        0    success
 */
int FileMv(const string& pm_sSrc, const string& pm_sDest);

/**
 *@brief  simply move file function
 *@param  pm_sFileName   file path
 *@return -1        fail
 *        0         success
 */
int FileRm(const string& pm_sFileName);

/**
 *@brief  create a specific directory
 *@param  dirPath   the specific directory
 *@return false       success
 *        true        failure
 */
bool createFileDir(const string& dirPath);

/**
*@desc list all the filepath or filename in the specific directory
*@param dirPath the search directory
*@param files the output filepath or filename containers
*@param fullPath if the output result is full file path with filename
*/
void listAllFiles(const std::string& dirPath, vector<string>& files, bool fullPath=false);

/**
 *@brief get the date string with the pattern: YYYYMMDD_HHMMSS
 *@return time string
 */
string currentTimestamp();

/**
 *@brief get the full path of stored file
 *@param path the base directory of the file
 *@param fileID the file id
 *@param fileName the file name 
 *@param filename the output full file path
 *@param filename_bak the output full backup file path
 */
void createDfsFileNames(const string& path, const string& fileID, const string& fileName, string& filename, string& filename_bak);


/**
 *@desc splite string by the dilimiter character
 *@param pm_sExpression string to splite
 *@param pm_cDelimiter the dilimiter
 *@param pm_vecArray the splited substrings
 */
void SplitString(const string &pm_sExpression, char pm_cDelimiter, vector<string> &pm_vecArray);

void createDirectoryByContainer(const string& base, const string &container, string &directory);

}
}
}

#endif // _COMMON_H
