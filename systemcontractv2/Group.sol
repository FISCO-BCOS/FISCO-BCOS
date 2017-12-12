pragma solidity ^0.4.4;

contract Group {
    mapping (bytes32 => bool) private _permissions; //权限列表，key为sha256(合约地址, 函数名)
    bool private _create = false; //是否可创建合约
    bool private _black = false; //黑名单模式
    
    function getPermission(address to, string func) public constant returns(bool) {
        return true;//所有人都有权限

        bytes32 key = sha256(to, func);
        
        if(_black) {
            return !_permissions[key];
        }
        
        return _permissions[key];
    }
    
    function setPermission(address to, string func, bool perrmission) public {
        bytes32 key = sha256(to, func);
        
        _permissions[key] = perrmission;
    }
    
    function getCreate() constant public returns(bool) {
        return _create;
    }
    
    function setCreate(bool create) public {
        _create = create;
    }
    
    function getBlack() constant public returns(bool) {
        return _black;
    }
    
    function setBlack(bool black) public {
        _black = black;
    }
}