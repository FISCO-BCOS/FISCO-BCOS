pragma solidity ^0.4.4;

contract Group {
	mapping (bytes32 => bool) private _permissions;		// 权限列表，key为sha256(合约地址, 函数名)
	bool private _create = false;						// 是否可创建合约
	bool private _black = false;						// 黑名单模式
	string private _desc;								// Group描述
	bytes32[] private _keys;
	mapping (bytes32 => string) private _keyMaptoFunc;
	
	function Group() public {
	}
	//event log1(address to,string func,bool permission);
    function getPermission(address to, string func) public constant returns(bool) {
		bytes32 key = sha256(to, func);
		//log1(to,func,_permissions[key]);
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
		//log1(to,func,permission);
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
