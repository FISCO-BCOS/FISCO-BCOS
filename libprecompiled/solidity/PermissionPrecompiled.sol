pragma solidity ^0.4.24;

contract PermissionPrecompiled 
{
    function insert(string table_name, string addr) public returns(int256);
    function remove(string table_name, string addr) public returns(int256);
    function queryByName(string table_name) public constant returns(string);
}
