pragma solidity ^0.4.4;
import "TransactionFilterBase.sol";
import "Group.sol";

contract Authority is TransactionFilterBase {
    bool private _enabled = false;                  // Whether a filter can check the permissions
    mapping (address => address) private _groups;   // Account map to group
    
    function setUserGroup(address user, address group) public {
        _groups[user] = group;
    }
    
    function getUserGroup(address user) public constant returns(address) {
        return _groups[user];
    }
    
    function enable() {
        _enabled = true;
    }
    
    // Check whether the user has the permissions to call a function
    function process(address origin, address from, address to, string func, string input) public constant returns(bool) {
        if(!_enabled) {
            return true;
        }
        
        return Group(_groups[origin]).getPermission(to, func);
    }
    
    function newGroup() public returns(address) {
        return new Group();
    }
}