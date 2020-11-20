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
/** @file CryptoInterface.cpp
 * @author xingqiangbai
 * @date 2020-04-22
 *
 */

#include "CryptoInterface.h"
#include "AES.h"
#include "ECDSASignature.h"
#include "Hash.h"
#include "SM2Signature.h"
#include "SM3Hash.h"
#include "SM4Crypto.h"
#include "libutilities/RLP.h"
#include <libconfig/GlobalConfigure.h>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

h256 bcos::EmptyHash = keccak256(bytesConstRef());
h256 bcos::EmptyTrie = keccak256(rlp(""));

std::function<std::string(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    bcos::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(bcos::crypto::aesCBCEncrypt);
std::function<std::string(const unsigned char* _encryptedData, size_t _encryptedDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    bcos::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(bcos::crypto::aesCBCDecrypt);
std::function<std::shared_ptr<crypto::Signature>(RLP const& _rlp, size_t _start)>
    bcos::crypto::SignatureFromRLP = ecdsaSignatureFromRLP;
std::function<std::shared_ptr<crypto::Signature>(std::vector<unsigned char>)>
    bcos::crypto::SignatureFromBytes = ecdsaSignatureFromBytes;

std::function<std::shared_ptr<crypto::Signature>(KeyPair const& _keyPair, const h256& _hash)>
    bcos::crypto::Sign = ecdsaSign;
std::function<bool(h512 const& _pubKey, std::shared_ptr<crypto::Signature> _sig, const h256& _hash)>
    bcos::crypto::Verify = ecdsaVerify;
std::function<h512(std::shared_ptr<crypto::Signature> _sig, const h256& _hash)>
    bcos::crypto::Recover = ecdsaRecover;

size_t bcos::crypto::signatureLength()
{
    if (g_BCOSConfig.SMCrypto())
    {
        // SM2SignatureLength;
        return 128;
    }
    // ECDSASignatureLength;
    return 65;
}

void bcos::crypto::initSMCrypto()
{
    EmptyHash = sm3(bytesConstRef());
    EmptyTrie = sm3(rlp(""));
    SignatureFromRLP = sm2SignatureFromRLP;
    SignatureFromBytes = sm2SignatureFromBytes;
    bcos::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(bcos::crypto::sm4Encrypt);
    bcos::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(bcos::crypto::sm4Decrypt);
    Sign = sm2Sign;
    Verify = sm2Verify;
    Recover = sm2Recover;
}

void bcos::crypto::initCrypto()
{
    EmptyHash = keccak256(bytesConstRef());
    EmptyTrie = keccak256(rlp(""));
    SignatureFromRLP = ecdsaSignatureFromRLP;
    SignatureFromBytes = ecdsaSignatureFromBytes;
    bcos::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(bcos::crypto::aesCBCEncrypt);
    bcos::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(bcos::crypto::aesCBCDecrypt);
    Sign = ecdsaSign;
    Verify = ecdsaVerify;
    Recover = ecdsaRecover;
}
