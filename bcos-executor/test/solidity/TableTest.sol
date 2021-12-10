pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

import "./precompiled/Table.sol";

contract TableTest {

    Table table;
    KVTable kv_table;
    constructor () public{
        table = Table(0x1001);
        table.createTable("t_test", "id","name,age");
        kv_table = KVTable(0x1009);
        kv_table.createTable("t_kv_test", "id", "item_price,item_name");
    }
    function select(string memory id) public view returns (string memory,string memory){
        CompareTriple memory compareTriple1 = CompareTriple("id",id,Comparator.EQ);
        CompareTriple[] memory compareFields = new CompareTriple[](1);
        compareFields[0] = compareTriple1;

        Condition memory condition;
        condition.condFields = compareFields;

        Entry[] memory entries = table.select("t_test", condition);
        string memory name;
        string memory age;
        if(entries.length > 0){
            name = entries[0].fields[0].value;
            age = entries[0].fields[1].value;
        }
        return (name,age);
    }

    function insert(string memory id,string memory name,string memory age) public returns (int256){
        KVField memory kv0 = KVField("id",id);
        KVField memory kv1 = KVField("name",name);
        KVField memory kv2 = KVField("age",age);
        KVField[] memory KVFields = new KVField[](3);
        KVFields[0] = kv0;
        KVFields[1] = kv1;
        KVFields[2] = kv2;
        Entry memory entry1 = Entry(KVFields);
        int256 result1 = table.insert("t_test",entry1);
        return result1;
    }

    function update(string memory id,string memory name,string memory age) public returns (int256){
        KVField memory kv1 = KVField("name",name);
        KVField memory kv2 = KVField("age",age);
        KVField[] memory KVFields = new KVField[](2);
        KVFields[0] = kv1;
        KVFields[1] = kv2;
        Entry memory entry1 = Entry(KVFields);

        CompareTriple memory compareTriple1 = CompareTriple("id",id,Comparator.EQ);
        CompareTriple[] memory compareFields = new CompareTriple[](1);
        compareFields[0] = compareTriple1;

        Condition memory condition;
        condition.condFields = compareFields;
        int256 result1 = table.update("t_test",entry1, condition);

        return result1;
    }

    function remove(string memory id) public returns(int256){
        CompareTriple memory compareTriple1 = CompareTriple("id",id,Comparator.EQ);
        CompareTriple[] memory compareFields = new CompareTriple[](1);
        compareFields[0] = compareTriple1;

        Condition memory condition;
        condition.condFields = compareFields;
        int256 result1 = table.remove("t_test", condition);

        return result1;
    }

    function createTableTest(string memory tableName,string memory key,string memory fields) public returns(int256){
        int256 result = table.createTable(tableName,key,fields);
        return result;
    }

    function descTest(string memory tableName) public returns(string memory, string memory){
        return table.desc(tableName);
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