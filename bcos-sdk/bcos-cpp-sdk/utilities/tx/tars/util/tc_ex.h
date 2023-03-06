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

#ifndef __TC_EX_H
#define __TC_EX_H

#include <stdexcept>
#include <string>

namespace tars
{

/////////////////////////////////////////////////
/** 
* @file  tc_ex.h 
* @brief 异常类 
* @brief Exception Class
*/           
/////////////////////////////////////////////////

/**
* @brief 异常类.
* @brief Exception Class
*/
class TC_Exception : public std::exception
{
public:
    /**
	 * @brief 构造函数，提供了一个可以传入errno的构造函数
     * @brief Constructor , provide a constructor which can pass errno
     * @param err, 是否附带系统错误信息(linux版本, 用errno获取错误码, windows版本, 用GetLastError()获取错误)
     * @param err  Whether system error information is included(linux: use errno to get error code, windows: use GetLastError() to get errors)
	 *  
	 * @param buffer 异常的告警信息 
     * @param buffer alert information of exceptions
     */
	explicit TC_Exception(const std::string &buffer);

    /**
	 * @brief 构造函数,提供了一个可以传入errno的构造函数，
     * @brief Constructor , provider a constructor to pass errno 
	 *  
	 *  	  异常抛出时直接获取的错误信息
     *        get error message directly when throw exception
	 *  
     * @param buffer 异常的告警信息
     * @param buffer alert information of exceptions
     * 
     * @param errno  传入:errno, windows版本 传入:GetLastError()
     * @param errno  pass:errno, windows version pass:GetLastError()
     */
	TC_Exception(const std::string &buffer, int err);

    /**
     * @brief 析够数函
     * @brief Destructor
     */
    virtual ~TC_Exception() throw();

    /**
     * @brief 错误信息.
     * @brief Error Message
     *
     * @return const char*
     */
    virtual const char* what() const throw();

    /**
     * @brief 获取错误码
     * @brief Get error code
     * 
     * @return 成功获取返回0
     * @return if get successfully , return 0
     */
    int getErrCode() { return _code; }

    /**
     * @brief 获取错误字符串(linux是errno, windows是GetLastError())
     * @brief Get the error std::string (linux: errno, windows: GetLastError())
     * 
     * @return 成功获取返回0
     * @return if get successfully , return 0
     */
    static std::string parseError(int err);

    /**
     * @brief 获取系统错误码(linux是errno, windows是GetLastError)
     * @brief Get system error code(linux: errno, windows: GetLastError())
     * 
     * @return 成功获取返回0
     * @return if get successfully , return 0
     */
    static int getSystemCode();

    /**
     * @brief 获取系统错误码(linux是errno, windows是GetLastError)
     *
     * @return 获取系统错误描述
     */
    static std::string getSystemError();

private:
    void getBacktrace();

private:

	/**
	 * 错误码
     * Error Code
     */
    int _code;
    /**
	 * 异常的相关信息
     * Information about exceptions
     */
    std::string _buffer;

};

//为了避免windows平台GetLastError()获取不对的问题, 因为抛异常, throw TC_Exception("xxxx", TC_Exception::getSystemCode())时
//In order to avoid getting the wrong getlasterror() on Windows platform, throw TC_ Exception("xxxx", TC_ Exception:: getsystemcode())
//回调用系统函数分配内存, 导致错误码被改写, 因此专门定义宏来抛出异常
//Callback uses system function to allocate memory, which causes error code to be rewritten, so it specially defines macro to throw exception
//先获取到错误码, 再throw
//Get the error code first, and then throw
#define THROW_EXCEPTION_SYSCODE(EX_CLASS, buffer) \
    {   \
    int ret = TC_Exception::getSystemCode(); \
    throw EX_CLASS(buffer, ret);              \
    }

}  // namespace tars
#endif


