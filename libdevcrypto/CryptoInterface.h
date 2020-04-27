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
/** @file CryptoInterface.h
 * @author xingqiangbai
 * @date 2020-04-22
 */

#pragma once

#include "Common.h"
#include "Hash.h"
#include "SM3Hash.h"
#include "Signature.h"
#include "libdevcore/FixedHash.h"
#include <functional>
#include <string>

namespace dev
{
extern h256 EmptyHash;
extern h256 EmptyTrie;

namespace crypto
{
using byte = unsigned char;
using bytes = std::vector<byte>;

extern std::function<std::string(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    SymmetricEncrypt;
extern std::function<std::string(const unsigned char* _encryptedData, size_t _encryptedDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    SymmetricDecrypt;

extern std::function<std::shared_ptr<Signature>(KeyPair const& _keyPair, const h256& _hash)> Sign;
extern std::function<bool(h512 const& _pubKey, std::shared_ptr<Signature> _sig, const h256& _hash)>
    Verify;
extern std::function<h512(std::shared_ptr<Signature> _sig, const h256& _hash)> Recover;
extern std::function<std::shared_ptr<Signature>(RLP const& _rlp, size_t _start)> SignatureFromRLP;
extern std::function<std::shared_ptr<Signature>(std::vector<unsigned char>)> SignatureFromBytes;

void initSMCtypro();

bool isSMCrypto();

size_t signatureLength();

template <unsigned N>
inline SecureFixedHash<32> Hash(SecureFixedHash<N>&& _data)
{
    if (isSMCrypto())
    {
        return sm3Secure(_data);
    }
    return sha3Secure(_data);
}

template <unsigned N>
inline SecureFixedHash<32> Hash(FixedHash<N>&& _data)
{
    if (isSMCrypto())
    {
        return sm3Secure(_data);
    }
    return sha3Secure(_data);
}

template <typename T>
inline h256 Hash(T&& _data)
{
    if (isSMCrypto())
    {
        return sm3(_data);
    }
    return sha3(_data);
}
}  // namespace crypto
}  // namespace dev
