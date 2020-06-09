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
/** @file Signature.h
 * @author xingqiangbai
 * @date 2020-04-26
 */

#pragma once

#include <libdevcore/FixedHash.h>
#include <libdevcore/RLP.h>

namespace dev
{
namespace crypto
{
struct Signature
{
    Signature() = default;
    virtual ~Signature() {}
    Signature(h256 const& _r, h256 const& _s) : r(_r), s(_s) {}
    virtual void encode(RLPStream& _s) const noexcept = 0;

    virtual std::vector<unsigned char> asBytes() const = 0;

    /// @returns true if r,s,v values are valid, otherwise false
    virtual bool isValid() const noexcept = 0;

    h256 r;
    h256 s;
};
}  // namespace crypto
}  // namespace dev
