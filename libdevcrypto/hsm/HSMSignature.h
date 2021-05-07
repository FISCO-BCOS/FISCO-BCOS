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
/** @file SDFSM2Signature.h
 * @author maggiewu
 * @date 2021-02-01
 */

#pragma once
#include "libdevcore/RLP.h"
#include "libdevcrypto/Common.h"
#include "libdevcrypto/Signature.h"

#include <vector>
namespace dev
{
namespace crypto
{
std::shared_ptr<crypto::Signature> SDFSM2Sign(KeyPair const& _keyPair, const h256& _hash);
bool SDFSM2Verify(h512 const& _pubKey, std::shared_ptr<crypto::Signature> _sig, const h256& _hash);
h512 SDFSM2Recover(std::shared_ptr<crypto::Signature> _sig, const h256& _hash);
std::shared_ptr<crypto::Signature> SDFSM2SignatureFromRLP(RLP const& _rlp, size_t _start);
std::shared_ptr<crypto::Signature> SDFSM2SignatureFromBytes(std::vector<unsigned char> _data);
}  // namespace crypto
}  // namespace dev
