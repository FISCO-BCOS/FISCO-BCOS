/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file TablePrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "libprecompiled/Precompiled.h"

namespace dev
{
#if 0
contract Table {
    function select(string, Condition) public constant returns(Entries);
    function insert(string, Entry) public returns(int);
    function update(string, Entry, Condition) public returns(int);
    function remove(string, Condition) public returns(int);
    function newEntry() public constant returns(Entry);
    function newCondition() public constant returns(Condition);
}
{
    "31afac36": "insert(string,address)",
    "7857d7c9": "newCondition()",
    "13db9346": "newEntry()",
    "28bb2117": "remove(string,address)",
    "e8434e39": "select(string,address)",
    "bf2b70a1": "update(string,address,address)"
}
#endif
namespace precompiled
{
class TablePrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<TablePrecompiled> Ptr;
    TablePrecompiled();
    virtual ~TablePrecompiled(){};


    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address(),
        Address const& _sender = Address()) override;

    std::shared_ptr<dev::storage::Table> getTable() { return m_table; }
    void setTable(std::shared_ptr<dev::storage::Table> table) { m_table = table; }

    h256 hash();

private:
    std::shared_ptr<storage::Table> m_table;
};

}  // namespace precompiled

}  // namespace dev
