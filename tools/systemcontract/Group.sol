pragma solidity ^0.4.4;

contract Group {
    mapping (bytes32 => bool) private _permissions;     // permission list, key is sha256(contract address, function name)
    bool private _create = false;                       // Whether a group can deploy a contract
    bool private _black = false;                        // Whether a group can use the blacklist
    string private _desc;                               // the description of group
    bytes32[] private _keys;                            // list, content is sha256(contract address, function name)
    mapping (bytes32 => string) private _keyMaptoFunc;  // sha256(contract address, function name) map to function name
	
    function Group() public {
    }

    function getPermission(address to, string func) public constant returns(bool) {
        bytes32 key = sha256(to, func);
        if(_black) {
            return !_permissions[key];
        }
        return _permissions[key];
    }
    
    function setDesc(string desc) public {
        _desc = desc;
    }
    function getDesc() public constant returns(string) {
        return _desc;
    }
	
    function setPermission(address to, string func, string funcDesc, bool permission) public {
        bytes32 key = sha256(to, func);
        if (permission) {
            bool bFlag = false;
            for (uint256 i = 0; i < _keys.length; i++) {
                if (_keys[i] == key) {
                    bFlag = true;
                    break;
                }
            }
            if (bFlag == false) {
                _keys.push(key);
                _keyMaptoFunc[key] = funcDesc;	
            }
        }
        _permissions[key] = permission;
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
	
    function getKeys() constant public returns(bytes32[]) {
        return _keys;
    }
	
    function getFuncDescwithPermissionByKey(bytes32 key) constant public returns(string) {
        if (_permissions[key]) {
            return _keyMaptoFunc[key];
        }
        else {
            return "";
        }
    }
}
