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
/** @file CRUDPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once
#include "Common.h"
#include "libstorage/Table.h"

namespace dev
{
namespace storage
{
class Table;
}

namespace precompiled
{
#if 0
contract CRUD
{
    function insert(string tableName, string key, string entry, string optional) public returns(int);
    function update(string tableName, string key, string entry, string condition, string optional) public returns(int);
    function remove(string tableName, string key, string condition, string optional) public returns(int);
    function select(string tableName, string key, string condition, string optional) public constant returns(string);
}
{
    "efc81a8c": "create()",
    "ebf3b24f": "insert(string,int256,string)",
    "c4f41ab3": "remove(string,int256)",
    "fcd7e3c1": "select(string)",
    "487a5a10": "update(string,int256,string)"
}
#endif

class CRUDPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<CRUDPrecompiled> Ptr;
    CRUDPrecompiled();
    virtual ~CRUDPrecompiled(){};

    virtual std::string toString();

    virtual bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address());

private:
    int parseEntry(const std::string& entryStr, storage::Entry::Ptr& entry);
    int parseCondition(const std::string& conditionStr, storage::Condition::Ptr& condition);
};

}  // namespace precompiled

}  // namespace dev
