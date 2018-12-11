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
/** @file MinerPrecompiled.h
 *  @author caryliao
 *  @date 20181204
 */
#pragma once
#include "libblockverifier/ExecutiveContext.h"

namespace dev
{
namespace blockverifier
{
#if 0
contract Authority {
    function insert(string table_name, string addr) public returns(uint);
    function remove(string table_name, string addr) public returns(uint);
}
{
    "06e63ff8": "insert(string,string)",
    "44590a7e": "remove(string,string)"
}
#endif

/// _sys_table_access_ table fields
const char* const SYS_AC_FIELD_TABLE_NAME = "table_name";
const char* const SYS_AC_FIELD_ADDRESS = "address";
const char* const SYS_AC_FIELD_ENABLENUM = "enable_num";

class AuthorityPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<AuthorityPrecompiled> Ptr;

    virtual ~AuthorityPrecompiled(){};

    virtual std::string toString(ExecutiveContext::Ptr);

    virtual bytes call(ExecutiveContext::Ptr context, bytesConstRef param, Address origin = Address());

protected:
    std::shared_ptr<storage::Table> openTable(
        ExecutiveContext::Ptr context, const std::string& tableName);
};

}  // namespace blockverifier

}  // namespace dev
