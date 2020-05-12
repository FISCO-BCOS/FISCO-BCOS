
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
/** @file KVTablePrecompiled.h
 *  @author xingqiangbai
 *  @date 20200206
 */
#pragma once

#include "libprecompiled/Precompiled.h"

namespace dev
{
namespace precompiled
{
#if 0
contract KVTable {
    function get(string) public constant returns(bool, Entry);
    function set(string, Entry) public returns(bool, int);
    function newEntry() public constant returns(Entry);
}
#endif

class KVTablePrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<KVTablePrecompiled> Ptr;
    KVTablePrecompiled();
    virtual ~KVTablePrecompiled(){};


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
