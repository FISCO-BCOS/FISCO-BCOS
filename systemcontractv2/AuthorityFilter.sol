pragma solidity ^0.4.4;
import "TransactionFilterBase.sol";
import "Group.sol";

contract AuthorityFilter is TransactionFilterBase {
	bool private _enabled = false; 					// 是否启用权限控制
	mapping (address => address) private _groups; 	// 用户对应的角色
	
    function AuthorityFilter() public {
    }
    function getInfo() public constant returns(string,string,string) {
        return (_name,_version,_desc);
    }
	
    //设置用户的角色
    function setUserToNewGroup(address user) public {
		_groups[user] = new Group();
    }
	function setUserToGroup(address user, address group) public {
		_groups[user] = group;
    }
    //获取用户的角色
    function getUserGroup(address user) public constant returns(address) {
        return _groups[user];
    }
    
	//设置启用权限控制状态
    function setEnable(bool enable) public {
        _enabled = enable;
    }
	//获取是否启用权限控制状态
	function getEnable() public constant returns(bool) {
		return _enabled;
	}

    //检查用户某个操作的权限
    function process(address origin, address from, address to, string func, string input) public constant returns(bool) {
		if (!_enabled) {
            return true;
        }
        address group = Group(_groups[origin]);
        if (0x0000000000000000000000000000000000000000 == group) {
			return false;
        }
		return Group(group).getPermission(to, func);
    }
	function deploy(address origin) public constant returns(bool) {
		if (!_enabled) {
            return true;
        }
		address group = Group(_groups[origin]);
        if (0x0000000000000000000000000000000000000000 == group) {
			return false;
        }
		return Group(group).getCreate();
	}	
	
	//新建一个角色
    function newGroup(string desc) public returns(address) {
        address group = new Group();
		Group(group).setDesc(desc);
		return group;
    }
}