/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file TablePrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/Common.h>

namespace dev
{
namespace blockverifier
{
#if 0
contract DB {
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

class TablePrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<TablePrecompiled> Ptr;

    virtual ~TablePrecompiled(){};


    virtual std::string toString(std::shared_ptr<ExecutiveContext>) override;

    virtual bytes call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param) override;

    dev::storage::Table::Ptr getTable() { return m_table; }
    void setTable(dev::storage::Table::Ptr table) { m_table = table; }

    h256 hash();

private:
    dev::storage::Table::Ptr m_table;
};

}  // namespace blockverifier

}  // namespace dev
