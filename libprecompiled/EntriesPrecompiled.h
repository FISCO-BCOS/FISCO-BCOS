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
/** @file EntriesPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "libstorage/Table.h"
#include <libblockverifier/ExecutiveContext.h>

namespace dev
{
namespace blockverifier
{
#if 0
contract Entries {
    function get(int) public constant returns(Entry);
    function size() public constant returns(int);
}
{
    "846719e0": "get(int256)",
    "949d225d": "size()"
}
#endif

class EntriesPrecompiled : public Precompiled
{
public:
    typedef std::shared_ptr<EntriesPrecompiled> Ptr;
    EntriesPrecompiled();
    virtual ~EntriesPrecompiled(){};

    virtual std::string toString();

    virtual bytes call(
        ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin = Address());

    void setEntries(dev::storage::Entries::ConstPtr entries) { m_entriesConst = entries; }
    dev::storage::Entries::Ptr getEntries()
    {
        return std::const_pointer_cast<dev::storage::Entries>(m_entriesConst);
    }
    dev::storage::Entries::ConstPtr getEntries() const { return m_entriesConst; }

private:
    dev::storage::Entries::ConstPtr m_entriesConst;
};

}  // namespace blockverifier

}  // namespace dev
