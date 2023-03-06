/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed 
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 * specific language governing permissions and limitations under the License.
 */

#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_ex.h>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_platform.h>
#if TARGET_PLATFORM_LINUX
#include <execinfo.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <cerrno>
#include <iostream>

namespace tars
{

TC_Exception::TC_Exception(const std::string &buffer)
: _code(0), _buffer(buffer)
{
}


TC_Exception::TC_Exception(const std::string &buffer, int err)
{
    if(err != 0)
    {
    	_buffer = buffer + " :" + parseError(err);
    }
    else
    {
        _buffer = buffer;    
    }
    _code   = err;
}

TC_Exception::~TC_Exception() throw()
{
}

const char* TC_Exception::what() const throw()
{
    return _buffer.c_str();
}

void TC_Exception::getBacktrace()
{
#if TARGET_PLATFORM_LINUX
    void * array[64];
    int nSize = backtrace(array, 64);
    char ** symbols = backtrace_symbols(array, nSize);

    for (int i = 0; i < nSize; i++)
    {
        _buffer += symbols[i];
        _buffer += "\n";
    }
	free(symbols);
#endif
}

#if TARGET_PLATFORM_WINDOWS
static std::string Unicode2ANSI(LPCWSTR lpszSrc)
{
    std::string sResult;
    if (lpszSrc != NULL) {
        int  nANSILen = WideCharToMultiByte(CP_ACP, 0, lpszSrc, -1, NULL, 0, NULL, NULL);
        char* pANSI = new char[nANSILen + 1];
        if (pANSI != NULL) {
            ZeroMemory(pANSI, nANSILen + 1);
            WideCharToMultiByte(CP_ACP, 0, lpszSrc, -1, pANSI, nANSILen, NULL, NULL);
            sResult = pANSI;
            delete[] pANSI;
        }
    }
    return sResult;
}
#endif

std::string TC_Exception::parseError(int err)
{
    std::string errMsg;

#if TARGET_PLATFORM_LINUX || TARGET_PLATFORM_IOS
    errMsg = strerror(err);
#else
    // LPTSTR lpMsgBuf;
    LPSTR lpMsgBuf;

    FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, err, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
            (LPTSTR) & lpMsgBuf, 0, NULL);

    // errMsg = Unicode2ANSI((LPCWSTR)lpMsgBuf);
    if(lpMsgBuf != NULL)
    {
        errMsg = lpMsgBuf;
    }
    LocalFree(lpMsgBuf);
#endif

    return errMsg;
}

int TC_Exception::getSystemCode()
{
#if TARGET_PLATFORM_WINDOWS        
    return GetLastError();
#else
    return errno; 
#endif
}

std::string TC_Exception::getSystemError()
{
    return parseError(getSystemCode());
}
}
