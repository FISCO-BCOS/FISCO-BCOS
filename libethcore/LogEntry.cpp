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
/** @file LogEntry.cpp
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth
 */

#include "LogEntry.h"

#include <libdevcrypto/CryptoInterface.h>
#include <libutilities/RLP.h>

namespace bcos
{
namespace eth
{
LogEntry::LogEntry(RLP const& _r)
{
    assert(_r.itemCount() == 3);
    address = (Address)_r[0];
    topics = _r[1].toVector<h256>();
    data = _r[2].toBytes();
}

void LogEntry::streamRLP(RLPStream& _s) const
{
    _s.appendList(3) << address << topics << data;
}

LogBloom LogEntry::bloom() const
{
    LogBloom ret;
    ret.shiftBloom<3>(crypto::Hash(address.ref()));
    for (auto t : topics)
        ret.shiftBloom<3>(crypto::Hash(t.ref()));
    return ret;
}

}  // namespace eth
}  // namespace bcos
