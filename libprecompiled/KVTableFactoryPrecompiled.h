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
/** @file KVTableFactoryPrecompiled.h
 *  @author xingqiangbai
 *  @date 20200206
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
contract KVTableFactory {
    function openTable(string) public constant returns (KVTable);
    function createTable(string, string, string) public returns (bool,int);
}
#endif

class KVTableFactoryPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<KVTableFactoryPrecompiled> Ptr;
    KVTableFactoryPrecompiled();
    virtual ~KVTableFactoryPrecompiled(){};

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
