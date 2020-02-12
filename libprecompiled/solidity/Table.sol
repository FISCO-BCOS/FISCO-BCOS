pragma solidity ^0.4.24;

contract TableFactory {
    function openTable(string) public view returns (Table); //open table
    function createTable(string, string, string) public returns (int256); //create table
}

//select condition
contract Condition {
    function EQ(string, int256) public;
    function EQ(string, string) public;

    function NE(string, int256) public;
    function NE(string, string) public;

    function GT(string, int256) public;
    function GE(string, int256) public;

    function LT(string, int256) public;
    function LE(string, int256) public;

    function limit(int256) public;
    function limit(int256, int256) public;
}

//one record
contract Entry {
    function getInt(string) public view returns (int256);
    function getUInt(string) public view returns (int256);
    function getAddress(string) public view returns (address);
    function getBytes64(string) public view returns (bytes1[64]);
    function getBytes32(string) public view returns (bytes32);
    function getString(string) public view returns (string);

    function set(string, int256) public;
    function set(string, uint256) public;
    function set(string, string) public;
    function set(string, address) public;
}

//record sets
contract Entries {
    function get(int256) public view returns (Entry);
    function size() public view returns (int256);
}

//Table main contract
contract Table {
    function select(string, Condition) public view returns (Entries);
    function insert(string, Entry) public returns (int256);
    function update(string, Entry, Condition) public returns (int256);
    function remove(string, Condition) public returns (int256);

    function newEntry() public view returns (Entry);
    function newCondition() public view returns (Condition);
}

contract KVTableFactory {
    function openTable(string) public view returns (KVTable);
    function createTable(string, string, string) public returns (int256);
}

//KVTable per permiary key has only one Entry
contract KVTable {
    function get(string) public view returns (bool, Entry);
    function set(string, Entry) public returns (int256);
    function newEntry() public view returns (Entry);
}
