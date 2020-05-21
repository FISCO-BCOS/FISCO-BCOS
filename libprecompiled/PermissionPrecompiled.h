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

class PermissionPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<PermissionPrecompiled> Ptr;
    PermissionPrecompiled();
    virtual ~PermissionPrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address(),
        Address const& _sender = Address()) override;

protected:
    void addPrefixToUserTable(std::string& tableName);

private:
    std::string queryPermission(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        const std::string& tableName);
    int revokeWritePermission(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        const std::string& tableName, const std::string& user, Address const& origin);
    bool checkPermission(std::shared_ptr<blockverifier::ExecutiveContext> context,
        std::string const& tableName, Address const& origin);
};

}  // namespace precompiled

}  // namespace dev
