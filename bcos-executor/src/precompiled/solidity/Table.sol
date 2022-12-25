// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;
import "./EntryWrapper.sol";

// KeyOrder指定Key的排序规则，字典序和数字序，如果指定为数字序，key只能为数字
enum KeyOrder {Lexicographic, Numerical}
struct TableInfo {
    KeyOrder keyOrder;
    string keyColumn;
    string[] valueColumns;
}

// 更新字段，用于update
struct UpdateField {
    string columnName;
    // 考虑工具类
    string value;
}

// 筛选条件，大于、大于等于、小于、小于等于
enum ConditionOP {GT, GE, LT, LE, EQ, NE, STARTS_WITH, ENDS_WITH, CONTAINS}
struct Condition {
    ConditionOP op;
    string field;
    string value;
}

// 数量限制
struct Limit {
    uint32 offset;
    // count limit max is 500
    uint32 count;
}

// 表管理合约，是静态Precompiled，有固定的合约地址
abstract contract TableManager {
    // 创建表，传入TableInfo
    function createTable(string memory path, TableInfo memory tableInfo) public virtual returns (int32);

    // 创建KV表，传入key和value字段名
    function createKVTable(string memory tableName, string memory keyField, string memory valueField) public virtual returns (int32);

    // 只提供给Solidity合约调用时使用
    function openTable(string memory path) public view virtual returns (address);

    // 变更表字段
    // 只能新增字段，不能删除字段，新增的字段默认值为空，不能与原有字段重复
    function appendColumns(string memory path, string[] memory newColumns) public virtual returns (int32);

    // 获取表信息
    function descWithKeyOrder(string memory tableName) public view virtual returns (TableInfo memory);
}

// 表合约，是动态Precompiled，TableManager创建时指定地址
abstract contract Table {
    // 按key查询entry
    function select(string memory key) public virtual view returns (Entry memory);

    // 按条件批量查询entry，condition为空则查询所有记录
    function select(Condition[] memory conditions, Limit memory limit) public virtual view returns (Entry[] memory);

    // 按照条件查询count数据
    function count(Condition[] memory conditions) public virtual view returns (uint32);

    // 插入数据
    function insert(Entry memory entry) public virtual returns (int32);

    // 按key更新entry
    function update(string memory key, UpdateField[] memory updateFields) public virtual returns (int32);

    // 按条件批量更新entry，condition为空则更新所有记录
    function update(Condition[] memory conditions, Limit memory limit, UpdateField[] memory updateFields) public virtual returns (int32);

    // 按key删除entry
    function remove(string memory key) public virtual returns (int32);
    // 按条件批量删除entry，condition为空则删除所有记录
    function remove(Condition[] memory conditions, Limit memory limit) public virtual returns (int32);
}

abstract contract KVTable {
    function get(string memory key) public view virtual returns (bool, string memory);

    function set(string memory key, string memory value) public virtual returns (int32);
}