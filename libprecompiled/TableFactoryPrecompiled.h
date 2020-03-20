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
/** @file TableFactoryPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "libprecompiled/Precompiled.h"

namespace dev
{
namespace storage
{
class MemoryTableFactory;
}

namespace precompiled
{
#if 0
{
    "56004b6a": "createTable(string,string,string)",
    "f23f63c9": "openTable(string)"
}
contract TableFactory {
    function openTable(string) public constant returns (Table);
    function createTable(string, string, string) public returns (int);
}
#endif

class TableFactoryPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<TableFactoryPrecompiled> Ptr;
    TableFactoryPrecompiled();
    virtual ~TableFactoryPrecompiled(){};

    std::string toString() override;

    bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context, bytesConstRef param,
        Address const& origin = Address(), Address const& _sender = Address()) override;

    void setMemoryTableFactory(std::shared_ptr<dev::storage::TableFactory> memoryTableFactory)
    {
        m_memoryTableFactory = memoryTableFactory;
    }

    std::shared_ptr<dev::storage::TableFactory> getMemoryTableFactory()
    {
        return m_memoryTableFactory;
    }

    h256 hash();

private:
    std::shared_ptr<dev::storage::TableFactory> m_memoryTableFactory;
};

}  // namespace precompiled

}  // namespace dev
