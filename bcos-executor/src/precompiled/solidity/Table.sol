// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

    // KeyOrder指定Key的排序规则，字典序和数字序，如果指定为数字序，key只能为数字
    // enum KeyOrder {Lexicographic, Numerical}
    struct TableInfo {
        string keyColumn;
        string[] valueColumns;
    }

    // 记录，用于select和insert
    struct Entry {
        string key;
        string[] fields; // 考虑2.0的Entry接口，临时Precompiled的问题，考虑加工具类接口
    }

    // 更新字段，用于update
    struct UpdateField {
        uint index;
        // string columnName;
        // 考虑工具类
        string value;
    }

    // 筛选条件，大于、大于等于、小于、小于等于
    enum ConditionOP {GT, GE, LT, LE}
    struct Condition {
        ConditionOP op;
        string value;
    }

    // 数量限制
    struct Limit {
        uint offset;
        uint count;
    }

// 表管理合约，是静态Precompiled，有固定的合约地址
abstract contract TableManager {
    // 创建表，传入TableInfo ——是否只允许用sdk建表，链外建表
    function createTable(string memory path, TableInfo memory tableInfo) public virtual returns (int);

    // 变更表字段
    // 原本TableInfo的字段必须全部存在，只能新增字段，不能删除字段，新增的字段默认值为空
    function appendColumns(string memory path, string[] memory newColumns) public virtual returns (int);
}

// 表合约，是动态Precompiled，TableManager创建时指定地址
abstract contract Table {
    // 获取表信息
    function desc() public virtual returns (TableInfo memory);

    // 按key查询entry
    function select(string memory key) public virtual view returns (Entry memory);

    // 按条件批量查询entry，condition为空则查询所有记录
    function select(Condition[] memory conditions, Limit memory limit) public virtual view returns (Entry[] memory);

    // 按condition查询符合条件的entry数——count是否要独立存在
    function count(Condition[] memory conditions, Limit memory limit) public virtual view returns (int);

    // 插入数据
    function insert(Entry memory entry) public virtual returns (int);

    // 按key更新entry
    function update(string memory key, UpdateField[] memory updateFields) public virtual returns (int);

    // 按条件批量更新entry，condition为空则更新所有记录
    function update(Condition[] memory conditions, Limit memory limit, UpdateField[] memory updateFields) public virtual returns (int);

    // 按key删除entry
    function remove(string memory key) public virtual returns(int);
    // 按条件批量删除entry，condition为空则删除所有记录
    function remove(Condition[] memory conditions, Limit memory limit) public virtual returns (int);
}

contract KVTable {
    function createTable(string memory tableName, string memory key, string memory valueFields) public returns (int256){}
    function get(string memory tableName, string memory key) public view returns (bool, Entry memory){}
    function set(string memory tableName,string memory key, Entry memory) public returns (int256){}
}
