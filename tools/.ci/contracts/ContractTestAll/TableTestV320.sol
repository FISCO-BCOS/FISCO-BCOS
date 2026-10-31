// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/TableTestV320.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

import "./TableV320.sol";
import "./Cast.sol";

contract TableTestV320 {
    event CreateResult(int256 count);
    event InsertResult(int256 count);
    event UpdateResult(int256 count);
    event RemoveResult(int256 count);

    Cast constant cast =  Cast(address(0x100f));  
    TableManager constant tm =  TableManager(address(0x1002));
    Table table;
    string constant TABLE_NAME = "t_testV320";
    constructor () public {
        // create table
        string[] memory columnNames = new string[](3);
        columnNames[0] = "name";
        columnNames[1] = "age";
        columnNames[2] = "status";
        TableInfo memory tf = TableInfo(KeyOrder.Numerical ,"id", columnNames);

        tm.createTable(TABLE_NAME, tf);
        address t_address = tm.openTable(TABLE_NAME);
        require(t_address!=address(0x0),"");
        table = Table(t_address);
    }

    function getTableAddress() public view returns (address){
        return address(table);
    }

    function select(int64 id) public view returns (string memory, string memory)
    {
        Entry memory entry = table.select(cast.s64ToString(id));
        string memory name;
        string memory age;
        if(entry.fields.length == 3){
            name = entry.fields[0];
            age = entry.fields[1];
        }
        return (name, age);
    }

    function insert(int64 id, string memory name, string  memory age) public returns (int32){
        Entry memory entry = Entry(cast.s64ToString(id), new string[](3));
        entry.fields[0] = name;
        entry.fields[1] = age;
        entry.fields[2] = "init";
        int32 result = table.insert(entry);
        emit InsertResult(result);
        return result;
    }

    function update(int64 id, string memory name, string memory age) public returns (int32){
        UpdateField[] memory updateFields = new UpdateField[](2);
        updateFields[0] = UpdateField("name", name);
        updateFields[1] = UpdateField("age", age);

        int32 result = table.update(cast.s64ToString(id), updateFields);
        emit UpdateResult(result);
        return result;
    }

    function remove(int64 id) public returns(int32){
        int32 result = table.remove(cast.s64ToString(id));
        emit RemoveResult(result);
        return result;
    }

    function select(int64 idLow, int64 idHigh) public view returns (string[] memory)
    {
        Limit memory limit = Limit(0, 500);
        Condition[] memory cond = new Condition[](2);
        cond[0] = Condition(ConditionOP.GT, "id", cast.s64ToString(idLow));
        cond[1] = Condition(ConditionOP.LE, "id", cast.s64ToString(idHigh));
        Entry[] memory entries = table.select(cond, limit);
        string[] memory names = new string[](entries.length);
        for(uint i = 0; i < names.length; i++)
        {
            names[i] = entries[i].fields[0];
        }
        return names;
    }

    function count(int64 idLow, int64 idHigh) public view returns (uint32)
    {
        Condition[] memory cond = new Condition[](2);
        cond[0] = Condition(ConditionOP.GT, "id", cast.s64ToString(idLow));
        cond[1] = Condition(ConditionOP.LE, "id", cast.s64ToString(idHigh));
        return  table.count(cond);
    }

    function update(int64 idLow, int64 idHigh) public returns (int32)
    {
        UpdateField[] memory updateFields = new UpdateField[](1);
        updateFields[0] = UpdateField("status", "updated");

        Limit memory limit = Limit(0, 500);
        Condition[] memory cond = new Condition[](2);
        cond[0] = Condition(ConditionOP.GT, "id", cast.s64ToString(idLow));
        cond[1] = Condition(ConditionOP.LE, "id", cast.s64ToString(idHigh));
        return table.update(cond, limit, updateFields);
    }

    function remove(int64 idLow, int64 idHigh) public returns (int32)
    {
        Limit memory limit = Limit(0, 500);
        Condition[] memory cond = new Condition[](2);
        cond[0] = Condition(ConditionOP.GT, "id", cast.s64ToString(idLow));
        cond[1] = Condition(ConditionOP.LE, "id", cast.s64ToString(idHigh));
        return table.remove(cond, limit);
    }

    function createTable(string memory tableName, uint8 keyOrder, string memory key,string[] memory fields) public returns(int256){
        require(keyOrder == 0 || keyOrder == 1);
        KeyOrder _keyOrder = KeyOrder.Lexicographic;
        if (keyOrder == 1)
        {
            _keyOrder = KeyOrder.Numerical;
        }
        TableInfo memory tf = TableInfo(_keyOrder, key, fields);
        int32 result = tm.createTable(tableName,tf);
        emit CreateResult(result);
        return result;
    }

    function desc() public view returns(string memory, string[] memory){
        TableInfo memory ti = tm.descWithKeyOrder(TABLE_NAME);
        return (ti.keyColumn,ti.valueColumns);
    }
}