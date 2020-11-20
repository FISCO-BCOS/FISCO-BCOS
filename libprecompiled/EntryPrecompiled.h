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
/** @file EntryPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "libprecompiled/Precompiled.h"
#include "libstorage/Table.h"

namespace bcos
{
namespace storage
{
class Entry;
}
namespace precompiled
{
#if 0
contract Entry {
    function getInt(string) public constant returns(int);
    function getBytes32(string) public constant returns(bytes32);
    function getBytes64(string) public constant returns(byte[64]);
    function getAddress(string) public constant returns(address);

    function set(string, int) public;
    function set(string, string) public;
}
{
    "fda69fae": "getInt(string)",
    "d52decd4": "getBytes64(string)",
    "27314f79": "getBytes32(string)",
    "bf40fac1": "getAddress(string)"
    "2ef8ba74": "set(string,int256)",
    "e942b516": "set(string,string)",
}
#endif

class EntryPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<EntryPrecompiled> Ptr;
    EntryPrecompiled();
    virtual ~EntryPrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<bcos::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address(),
        Address const& _sender = Address()) override;

    void setEntry(bcos::storage::Entry::Ptr entry) { m_entry = entry; }
    bcos::storage::Entry::Ptr getEntry() const { return m_entry; };

private:
    bcos::storage::Entry::Ptr m_entry;
};

}  // namespace precompiled

}  // namespace bcos
