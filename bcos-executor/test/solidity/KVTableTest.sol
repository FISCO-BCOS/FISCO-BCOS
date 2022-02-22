// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

import "./precompiled/KVTable.sol";

contract KVTableTest {

    KVTable kv_table;
    constructor () public{
        kv_table = KVTable(0x1009);
        kv_table.createTable("t_kv_test", "id", "item_price,item_name");
    }

    function descTest(string memory tableName) public returns(string memory, string memory){
        return kv_table.desc(tableName);
    }

    function get(string memory id) public view returns (bool, string memory, string memory) {
        bool ok = false;
        Entry memory entry;
        (ok, entry) = kv_table.get("t_kv_test",id);
        string memory item_price;
        string memory item_name;
        if (ok) {
            item_price = entry.fields[0].value;
            item_name = entry.fields[1].value;
        }
        return (ok, item_price, item_name);
    }
    function set(string memory id, string memory item_price, string memory item_name)
    public
    returns (int256)
    {
        KVField memory kv1 = KVField("item_price",item_price);
        KVField memory kv2 = KVField("item_name",item_name);
        KVField[] memory KVFields = new KVField[](2);
        KVFields[0] = kv1;
        KVFields[1] = kv2;
        Entry memory entry1 = Entry(KVFields);

        // the first parameter length of set should <= 255B
        int256 count = kv_table.set("t_kv_test", id,entry1);
        return count;
    }
    function createKVTableTest(string memory tableName,string memory key,string memory fields) public returns(int256){
        int256 result = kv_table.createTable(tableName,key,fields);
        return result;
    }
}