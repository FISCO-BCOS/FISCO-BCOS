pragma solidity ^0.4.24;

contract CRUDPrecompiled {
    function insert(string tableName, string key, string entry, string)
        public
        returns (int256);
    function remove(string tableName, string key, string condition, string)
        public
        returns (int256);
    function select(string tableName, string key, string condition, string)
        public
        view
        returns (string);
    function desc(string tableName) public view returns (string, string);
    function update(
        string tableName,
        string key,
        string entry,
        string condition,
        string
    ) public returns (int256);
}
