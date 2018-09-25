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
/** @file EntryPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Table.h"
#include <libblockverifier/ExecutiveContext.h>

namespace dev
{
namespace blockverifier
{
#if 0
contract Entry {
    function getInt(string) public constant returns(int);
    function getString(string) public constant returns(String);

    function set(string, int) public;
    function set(string, string) public;
}
{
    "fda69fae": "getInt(string)",
    "2ef8ba74": "set(string,int256)",
    "e942b516": "set(string,string)",
    "d52decd4": "getBytes64(string)",
    "bf40fac1": "getAddress(string)"
}
#endif

class EntryPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<EntryPrecompiled> Ptr;

    virtual ~EntryPrecompiled(){};

    virtual std::string toString(std::shared_ptr<ExecutiveContext>);

    virtual bytes call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param);

    void setEntry(dev::storage::Entry::Ptr entry) { m_entry = entry; }
    dev::storage::Entry::Ptr getEntry() { return m_entry; }

private:
    dev::storage::Entry::Ptr m_entry;
};

}  // namespace blockverifier

}  // namespace dev
