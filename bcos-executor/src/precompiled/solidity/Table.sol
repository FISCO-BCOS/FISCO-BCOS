// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

    struct KVField {
        string key;
        string value;
    }

    struct Entry {
        KVField[] fields;
    }

    enum Comparator {EQ, NE, GT, GE, LT, LE}
    struct CompareTriple {
        string lvalue;
        string rvalue;
        Comparator cmp;
    }

    struct Condition {
        CompareTriple[] condFields;
    }
contract Table {
    function createTable(string memory tableName, string memory key, string memory valueFields) public returns (int256){}
    function select(string memory tableName, Condition memory) public view returns (Entry[] memory){}
    function insert(string memory tableName, Entry memory) public returns (int256){}
    function update(string memory tableName, Entry memory, Condition memory) public returns (int256){}
    function remove(string memory tableName, Condition memory) public returns (int256){}
    function desc(string memory tableName) public returns(string memory,string memory){}
}
contract KVTable {
    function createTable(string memory tableName, string memory key, string memory valueFields) public returns (int256){}
    function get(string memory tableName, string memory key) public view returns (bool, Entry memory){}
    function set(string memory tableName,string memory key, Entry memory) public returns (int256){}
}
