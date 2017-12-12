/**
 * @file: EthLog.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "EthLog.h"
#include <libdevcore/easylog.h>
#include <libdevcore/Common.h>
#include <string>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

/*
采用easylogging打印日志
level定义与easylogging++.h对应：
enum class Level : base::type::EnumType {
    /// @brief Generic level that represents all the levels. Useful when setting global configuration for all levels
    Global = 1, 
    /// @brief Information that can be useful to back-trace certain events - mostly useful than debug logs.
    Trace = 2,  
    /// @brief Informational events most useful for developers to debug application
    Debug = 4,
    /// @brief Severe error information that will presumably abort application
    Fatal = 8,
    /// @brief Information representing errors in application but application will keep running
    Error = 16,
    /// @brief Useful when application has potentially harmful situtaions
    Warning = 32,
    /// @brief Information that can be highly useful and vary with verbose logging level.
    Verbose = 64,
    /// @brief Mainly useful to represent current progress of application
    Info = 128,
    /// @brief Represents unknown level
    Unknown = 1010
    };
*/

u256 EthLog::ethcall(uint32_t level, std::string str)
{
    //目前支持：TRACE DEBUG FATAL ERROR WARNING INFO
    if(2 & level) 
        LOG(TRACE) << str;
    else if(4 & level)
        LOG(DEBUG) << str;
    else if(8 & level)
        LOG(FATAL) << str;
    else if(16 & level)
        LOG(ERROR) << str;
    else if(32 & level)
        LOG(WARNING) << str;
    else if(128 & level)
        LOG(INFO) << str;
    else
    {
        LOG(ERROR) << "ethcall log has no level-" << level;
        return 1;
    }
    
    return 0;
}

uint64_t EthLog::gasCost(uint32_t level, std::string str)
{
    return sizeof(level) + str.length();
}

}
}
