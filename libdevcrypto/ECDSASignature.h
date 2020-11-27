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
/** @file ECDSASignature.h
 * @author xingqiangbai
 * @date 2020-04-27
 */

#pragma once

#include "Signature.h"
#include "libutilities/FixedBytes.h"
#include "libutilities/RLP.h"
#include <vector>

namespace bcos
{
namespace crypto
{
struct ECDSASignature : public Signature
{
    ECDSASignature() = default;
    ~ECDSASignature() {}
    ECDSASignature(u256 const& _r, u256 const& _s, byte _v)
    {
        r = _r;
        s = _s;
        v = _v;
    }
    void encode(RLPStream& _s) const noexcept;
    std::vector<unsigned char> asBytes() const;

    /// @returns true if r,s,v values are valid, otherwise false
    bool isValid() const noexcept;

    unsigned char v;
};
}  // namespace crypto

class KeyPair;
std::shared_ptr<crypto::Signature> ecdsaSign(const KeyPair& _keyPair, const h256& _hash);
bool ecdsaVerify(h512 const& _pubKey, std::shared_ptr<crypto::Signature> _sig, const h256& _hash);
h512 ecdsaRecover(std::shared_ptr<crypto::Signature> _sig, const h256& _hash);
std::shared_ptr<crypto::Signature> ecdsaSignatureFromRLP(RLP const& _rlp, size_t _start);
std::pair<bool, bytes> ecRecover(bytesConstRef _in);
std::shared_ptr<crypto::Signature> ecdsaSignatureFromBytes(std::vector<unsigned char>);
}  // namespace bcos
