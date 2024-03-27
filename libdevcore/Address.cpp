/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "Address.h"
#include "Exceptions.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

namespace dev
{
Address const ZeroAddress;
Address const MaxAddress{"0xffffffffffffffffffffffffffffffffffffffff"};
Address const SystemAddress{"0xfffffffffffffffffffffffffffffffffffffffe"};

Address toAddress(std::string const& _s)
{
    try
    {
        auto b = fromHex(_s.substr(0, 2) == "0x" ? _s.substr(2) : _s, WhenError::Throw);
        if (b.size() == 20)
            return Address(b);
    }
    catch (BadHexCharacter&)
    {
    }
    BOOST_THROW_EXCEPTION(InvalidAddress());
}

std::shared_ptr<std::set<Address>> convertStringToAddressSet(
    std::string const& _addressListStr, std::string const& _splitStr)
{
    std::vector<std::string> addressList;
    boost::split(addressList, _addressListStr, boost::is_any_of(_splitStr));
    std::shared_ptr<std::set<Address>> addressSet = std::make_shared<std::set<Address>>();
    for (auto const& address : addressList)
    {
        if (address.length() == 0)
        {
            continue;
        }
        try
        {
            addressSet->insert(toAddress(address));
        }
        catch (const std::exception& e)
        {
            LOG(WARNING) << LOG_DESC("convert String to address failed")
                         << LOG_KV("_addressListStr", _addressListStr);
        }
    }
    return addressSet;
}
}  // namespace dev
