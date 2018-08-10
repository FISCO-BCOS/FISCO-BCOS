/**
* file LibEthLog.sol
* author jimmyshi
* time 2017-09-26
* Print log into eth
*/
pragma solidity ^0.4.2;

import "LibString.sol";
import "LibInt.sol";

library LibEthLog {
    using LibString for *;
    using LibInt for *;

    /*
    *   LibEthLog 库
    *   功能：
    *       在eth中打日志
    *   用法：
    *       1、依赖：
    *           LibString.sol，LibInt.sol，与本文件放到同一目录下
    *       2、定义：
    *           import "LibEthLog.sol"
    *           using LibEthLog for *;
    *       3、打印日志：
    *           LibEthLog.DEBUG().append("your log").append(uint_num).append(int_num).commit();
    *           
    *           日志级别： TRACE(),DEBUG(),FATAL(), ERROR(), WARNING(), INFO()
    *           append个数不限，append参数可支持string, uint系列，int系列
    *           最后以commit()结束
    */

    //EthLog的EthCallId定义
    function callId() internal constant returns(uint32) {
        return 0x10; //Log的callId是0x10，与libevm/ethcall/EthCallEntry.h中的定义对应
    }

    //日志buffer定义
    struct LogInfo {
        uint32 levelCode;
        string logStr;    
    }

    //日志buffer开始函数
    function logStart(uint32 levelCode)
        internal 
        returns (LogInfo)
    {
        LogInfo memory logInfo;
        logInfo.levelCode = levelCode;
        logInfo.logStr = "";
        return logInfo;
    }

    /*
    *   日志级别（levelCode）：
    *   TRACE   = 2
    *   DEBUG   = 4
    *   FATAL   = 8
    *   ERROR   = 16
    *   WARNING = 32
    *   INFO    = 128
    */

    //封装了各个日志级别的，日志buffer开始函数
    function TRACE() 
        internal 
        returns (LogInfo)
    {
        return logStart(2);
    }

    function DEBUG() 
        internal 
        returns (LogInfo)
    {
        return logStart(4);
    }

    function FATAL() 
        internal 
        returns (LogInfo)
    {
        return logStart(8);
    }    

    function ERROR() 
        internal 
        returns (LogInfo)
    {
        return logStart(16);
    }

    function WARNING() 
        internal 
        returns (LogInfo)
    {
        return logStart(32);
    }

    function INFO() 
        internal 
        returns (LogInfo)
    {
        return logStart(128);
    }

    //append，向日志buffer写数据，重载了接收各种数据类型的append
    function append(LogInfo _self, string str)  //string类型
        internal 
        returns (LogInfo)
    {
        _self.logStr = _self.logStr.concat(str);
        return _self;
    }

    function append(LogInfo _self, uint num)    //uint, uint8...uint256都可
        internal 
        returns (LogInfo)
    {
        _self.logStr = _self.logStr.concat(num.toString());
        return _self;
    }  

    function append(LogInfo _self, int num)     //int, int8...int256都可
        internal 
        returns (LogInfo)
    {
        _self.logStr = _self.logStr.concat(num.toString());
        return _self;
    }    
        //TODO: 重载更多类型的append函数


    //日志缓冲提交
    function commit(LogInfo _self)internal {
        println(_self.levelCode, _self.logStr);
    }
    
    //调用底层的ethcall
    function println(uint32 levelCode, string str) internal {
        uint32 call_id = callId();
        uint r;
        assembly{
            r := ethcall(call_id, levelCode, str, 0, 0, 0, 0, 0, 0, 0)
        }
    }


}

