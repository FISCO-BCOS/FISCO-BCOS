pragma solidity ^0.4.4;
import "TransactionFilterBase.sol";
import "Group.sol";

contract AuthorityFilter is TransactionFilterBase {
    bool private _enabled = false;                           // Whether a filter can check the permissions
    mapping (address => address) private _groups;            // Account map to group
	
    function AuthorityFilter() public {
    }
    function getInfo() public constant returns(string,string,string) {
        return (_name,_version,_desc);
    }
	
    // set user to a new group, with a group being built
    function setUserToNewGroup(address user) public {
        _groups[user] = new Group();
    }
    // set user to a existing group
    function setUserToGroup(address user, address group) public {
        _groups[user] = group;
    }
    function getUserGroup(address user) public constant returns(address) {
        return _groups[user];
    }

    function setEnable(bool enable) public {
        _enabled = enable;
    }
    function getEnable() public constant returns(bool) {
        return _enabled;
    }

    // Check whether the user has the permissions to call a function
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
    // Check whether the user has the permissions to deploy a contract
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
	
    function newGroup(string desc) public returns(address) {
        address group = new Group();
        Group(group).setDesc(desc);
        return group;
    }
}