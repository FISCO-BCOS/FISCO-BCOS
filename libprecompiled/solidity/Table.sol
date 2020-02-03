pragma solidity ^0.4.24;

contract TableFactory {
    function openTable(string) public constant returns (Table); //open table
    function createTable(string,string,string) public returns(int); //create table
}

//select condition
contract Condition {
    function EQ(string, int) public;
    function EQ(string, string) public;
    
    function NE(string, int) public;
    function NE(string, string)  public;

    function GT(string, int) public;
    function GE(string, int) public;
    
    function LT(string, int) public;
    function LE(string, int) public;
    
    function limit(int) public;
    function limit(int, int) public;
}

//one record 
contract Entry {
    function getInt(string) public constant returns(int);
    function getAddress(string) public constant returns(address);
    function getBytes64(string) public constant returns(byte[64]);
    function getBytes32(string) public constant returns(bytes32);
    function getString(string) public constant returns(string);
    
    function set(string, int) public;
    function set(string, string) public;
    function set(string, address) public;
}

//record sets
contract Entries {
    function get(int) public constant returns(Entry);
    function size() public constant returns(int);
}

//Table main contract
contract Table {
    //select api
    function select(string, Condition) public constant returns(Entries);
    //insert api
    function insert(string, Entry) public returns(int);
    //update api
    function update(string, Entry, Condition) public returns(int);
    //remove api
    function remove(string, Condition) public returns(int);
    
    function newEntry() public constant returns(Entry);
    function newCondition() public constant returns(Condition);
}