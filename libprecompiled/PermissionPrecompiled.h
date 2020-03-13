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
/** @file PermissionPrecompiled.h
 *  @author caryliao
 *  @date 20181204
 */
#pragma once
#include "Common.h"

namespace dev
{
namespace precompiled
{
#if 0
contract PermissionPrecompiled {
    function insert(string table_name, string addr) public returns(int256);
    function remove(string table_name, string addr) public returns(int256);
    function queryByName(string table_name) public constant returns(string);
}
#endif

/// _sys_table_access_ table fields
const std::string SYS_AC_TABLE_NAME = "table_name";
const std::string SYS_AC_ADDRESS = "address";
const std::string SYS_AC_ENABLENUM = "enable_num";

class PermissionPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<PermissionPrecompiled> Ptr;
    PermissionPrecompiled();
    virtual ~PermissionPrecompiled(){};

    virtual std::string toString();

    virtual bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address());

protected:
    void addPrefixToUserTable(std::string& tableName);

private:
    std::string queryPermission(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        const std::string& tableName);
    int revokeWritePermission(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        const std::string& tableName, const std::string& user, Address const& origin);
};

}  // namespace precompiled

}  // namespace dev
