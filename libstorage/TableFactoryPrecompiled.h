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
/** @file TableFactoryPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "MemoryTableFactory.h"
#include "TablePrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Common.h>
#include <map>

namespace dev
{
namespace blockverifier
{
const unsigned TABLE_NOT_EXISTS = 0;
const unsigned TABLE_ALREADY_OPEN = 1;
const unsigned TABLENAME_ALREADY_EXISTS = 2;
const unsigned TABLENAME_CONFLICT = 101;
#if 0
{
    "56004b6a": "createTable(string,string,string)",
    "c184e0ff": "openDB(string)",
    "f23f63c9": "openTable(string)"
}
contract DBFactory {
    function openDB(string) public constant returns (DB);
    function openTable(string) public constant returns (DB);
    function createTable(string, string, string) public constant returns (DB);
}
#endif

class TableFactoryPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<TableFactoryPrecompiled> Ptr;

    virtual ~TableFactoryPrecompiled(){};


    virtual std::string toString(std::shared_ptr<ExecutiveContext>);

    virtual bytes call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param);

    void setMemoryTableFactory(dev::storage::MemoryTableFactory::Ptr MemoryTableFactory)
    {
        m_MemoryTableFactory = MemoryTableFactory;
    }

    h256 hash(std::shared_ptr<ExecutiveContext> context);

    Address openTable(ExecutiveContext::Ptr context, const std::string& tableName);

private:
    unsigned isTableCreated(ExecutiveContext::Ptr context, const std::string& tableName,
        const std::string& keyField, const std::string& valueFiled);
    TablePrecompiled::Ptr getSysTable(ExecutiveContext::Ptr context);
    dev::storage::MemoryTableFactory::Ptr m_MemoryTableFactory;
    std::map<std::string, Address> m_name2Table;
    h256 m_hash;
};

}  // namespace blockverifier

}  // namespace dev
