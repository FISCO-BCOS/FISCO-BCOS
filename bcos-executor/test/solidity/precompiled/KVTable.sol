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

contract KVTable {
    function createTable(
        string memory tableName,
        string memory key,
        string memory valueFields
    ) public returns (int256) {}

    function get(string memory tableName, string memory key)
    public
    view
    returns (bool, Entry memory entry)
    {}

    function set(
        string memory tableName,
        string memory key,
        Entry memory entry
    ) public returns (int256) {}

    function desc(string memory tableName) public returns(string memory,string memory){}
}
