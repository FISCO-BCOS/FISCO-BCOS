/**
*@file      OwnerNamed.sol
*@author    yanze
*@time      2016-11-29
*@desc      the defination of OwnerNamed contract
*/

pragma solidity ^0.4.2;


import "LibString.sol";
import "LibInt.sol";

contract OwnerNamed {
    using LibString for *;
    using LibInt for *;
    
    address owner;
    
    function OwnerNamed() {
        owner = msg.sender;
    }

    function register(string _name, string _version) public {
        //rm.register(_name,_version);
    }
    
    function kill() public {
        if (msg.sender != owner) {
            return;
        }
    }
    
    //test begin
    function getOwner() constant public returns (string _ret) {
        _ret = owner.addrToAsciiString();
    }

    function getSender() constant public returns (string _ret) {
        _ret = msg.sender.addrToAsciiString();
    }

    uint errno = 0;

    function getErrno() constant returns (uint) {
        return errno;
    }

    /** 日志函数， 内容长度限制为32字符，返回值必须（勿删除） */
    function log(string _str) constant public returns(uint) {
        log0(bytes32(stringToUint(_str)));
        return 0;
    }

    /** 日志函数， 内容长度限制为32字符，返回值必须（勿删除） */
    function log(string _str, string _topic) constant public returns(uint) {
        log1(bytes32(stringToUint(_str)), bytes32(stringToUint(_topic)));
        return 0;
    }

    /** 日志函数， 内容长度限制为32字符，返回值必须（勿删除） */
    function log(string _str, string _str2, string _topic) constant public  returns(uint) {
        string memory logStr = "";
        logStr = logStr.concat(_str, _str2);
        log1(bytes32(stringToUint(logStr)), bytes32(stringToUint(_topic)));
    }

    /** 日志函数， 内容长度限制为32字符，返回值必须（勿删除） */
    function log(string _str, uint _ui) constant public returns(uint) {
        string memory logStr = "";
        logStr = logStr.concat(_str, _ui.toString());
        log0(bytes32(stringToUint(logStr)));
    }

    /** 日志函数， 内容长度限制为32字符，返回值必须（勿删除） */
    function log(string _str, int _i) constant public returns(uint) {
        string memory logStr = "";
        logStr = logStr.concat(_str, _i.toString());
        log0(bytes32(stringToUint(logStr)));
        return 0;
    }

    /** 日志函数， 内容长度限制为32字符，返回值必须（勿删除） */
    function log(string _str, address _addr) constant public returns(uint) {
        string  memory logStr = "";
        logStr = logStr.concat(_str, uint(_addr).toAddrString());
        log0(bytes32(stringToUint(logStr)));
        return 0;
    }

    /** string convert to uint256 */
    function stringToUint(string _str) constant returns(uint _ret) {
        uint len = bytes(_str).length;
        if (len > 32) {
            len = 32;
        }

        _ret = 0;
        for(uint i=0; i<32; ++i) {
            if (i < len)
                _ret = _ret*256 + uint8(bytes(_str)[i]);
            else
                _ret = _ret*256;
        }
    }
}
