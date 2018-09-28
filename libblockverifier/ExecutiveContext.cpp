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
/** @file ExecutiveContext.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */


#include "ExecutiveContext.h"
#include <libexecutivecontext/ExecutionResult.h>

namespace dev
{
namespace blockverifier
{
bytes ExecutiveContext::call(Address address, bytesConstRef param)
{
    try
    {
        LOG(TRACE) << "PrecompiledEngine call:" << _blockInfo.hash << " " << _blockInfo.number
                   << " " << address << " " << toHex(param);

        auto p = getPrecompiled(address);

        if (p)
        {
            bytes out = p->call(shared_from_this(), param);
            return out;
        }
        else
        {
            LOG(DEBUG) << "can't find address:" << address;
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Precompiled call error:" << e.what();

        throw dev::eth::PrecompiledError();
    }

    return bytes();
}

Address ExecutiveContext::registerPrecompiled(Precompiled::Ptr p)
{
    Address address(++_addressCount);

    _address2Precompiled.insert(std::make_pair(address, p));

    return address;
}


bool ExecutiveContext::isPrecompiled(Address address)
{
    LOG(TRACE) << "PrecompiledEngine isPrecompiled:" << _blockInfo.hash << " " << address;

    auto p = getPrecompiled(address);

    if (p)
    {
        LOG(DEBUG) << "internal contract:" << address;
    }

    return p.get() != NULL;
}

Precompiled::Ptr ExecutiveContext::getPrecompiled(Address address)
{
    LOG(TRACE) << "PrecompiledEngine getPrecompiled:" << _blockInfo.hash << " " << address;

    LOG(TRACE) << "address size:" << _address2Precompiled.size();
    auto itPrecompiled = _address2Precompiled.find(address);

    if (itPrecompiled != _address2Precompiled.end())
    {
        return itPrecompiled->second;
    }

    return Precompiled::Ptr();
}