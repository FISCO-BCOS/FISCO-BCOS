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
/** @file CommonJS.h
 * @authors:
 *   Gav Wood <i@gavwood.com>
 *   Marek Kotewicz <marek@ethdev.com>
 * @date 2014
 */

#pragma once

#include "Common.h"
#include <libdevcrypto/Common.h>
#include <libutilities/CommonJS.h>
#include <boost/algorithm/string.hpp>
#include <string>

namespace bcos
{
/// Leniently convert string to Public (h512). Accepts integers, "0x" prefixing, non-exact length.
inline Public jsToPublic(std::string const& _s)
{
    return jsToFixed<sizeof(bcos::Public)>(_s);
}

/// Leniently convert string to Secret (h256). Accepts integers, "0x" prefixing, non-exact length.
inline Secret jsToSecret(std::string const& _s)
{
    h256 d = jsToFixed<sizeof(bcos::Secret)>(_s);
    Secret ret(d);
    d.ref().cleanse();
    return ret;
}

/// Leniently convert string to Address (h160). Accepts integers, "0x" prefixing, non-exact length.
inline Address jsToAddress(std::string const& _s)
{
    return eth::toAddress(_s);
}

namespace eth
{
/// Convert to a block number, a bit like jsToInt, except that it correctly recognises "pending" and
/// "latest".
inline BlockNumber jsToBlockNumber(std::string const& _js)
{
    return boost::lexical_cast<BlockNumber>(jsToU256(_js));
}

}  // namespace eth
}  // namespace bcos
