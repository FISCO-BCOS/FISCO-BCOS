pragma solidity ^0.4.24;

contract CRUDPrecompiled 
{
    function insert(string tableName, string key, string entry, string optional) public returns(int);
    function update(string tableName, string key, string entry, string condition, string optional) public returns(int);
    function remove(string tableName, string key, string condition, string optional) public returns(int);
    function select(string tableName, string key, string condition, string optional) public constant returns(string);
}

