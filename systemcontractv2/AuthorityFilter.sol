pragma solidity ^0.4.4;
import "TransactionFilterBase.sol";
import "Group.sol";

contract AuthorityFilter is TransactionFilterBase {
    bool private _enabled = false; //是否启用权限控制
    mapping (address => address) private _groups; //用户对应的角色
    
    function AuthorityFilter(){
        
    }
    function getInfo()public returns(string,string){
        return (name,version);
    }
    //设置用户的角色
    function setUserGroup(address user, address group) public {
        _groups[user] = group;
    }
    
    //获取用户的角色
    function getUserGroup(address user) public constant returns(address) {
        return _groups[user];
    }
    
    function enable() {
        _enabled = true;
    }
    
    //检查用户某个操作的权限
    function process(address origin, address from, address to, string func, string input) public constant returns(bool) {
        if(!_enabled) {
            return true;
        }
        address group=Group(_groups[origin]);
        if( 0x0000000000000000000000000000000000000000 == group ){
            return false;
        }

        return Group(group).getPermission(to, func);
    }
    
    //新建一个角色
    function newGroup() public returns(address) {
        return new Group();
    }
}