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
#include "libdevcore/RLP.h"

using namespace std;
using namespace dev;

h256 dev::EmptyHash = sha3(bytesConstRef());
h256 dev::EmptyTrie = sha3(rlp(""));
static bool SMCrypto = false;
static size_t SM2SignatureLength = 128;
static size_t ECDSASignatureLength = 65;

std::function<std::string(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::aesCBCEncrypt);
std::function<std::string(const unsigned char* _encryptedData, size_t _encryptedDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::aesCBCDecrypt);
std::function<std::shared_ptr<Signature>(RLP const& _rlp, size_t _start)>
    dev::crypto::SignatureFromRLP = ECDSASignatureFromRLP;
std::function<std::shared_ptr<Signature>(std::vector<unsigned char>)>
    dev::crypto::SignatureFromBytes = ECDSASignatureFromBytes;

std::function<std::shared_ptr<Signature>(KeyPair const& _keyPair, const h256& _hash)>
    dev::crypto::Sign = ecdsaSign;
std::function<bool(h512 const& _pubKey, std::shared_ptr<Signature> _sig, const h256& _hash)>
    dev::crypto::Verify = ecdsaVerify;
std::function<h512(std::shared_ptr<Signature> _sig, const h256& _hash)> dev::crypto::Recover =
    ecdsaRecover;

bool dev::crypto::isSMCrypto()
{
    return SMCrypto;
}

size_t dev::crypto::signatureLength()
{
    if (isSMCrypto())
    {
        return SM2SignatureLength;
    }
    return ECDSASignatureLength;
}

void dev::crypto::initSMCtypro()
{
    SMCrypto = true;
    EmptyHash = sm3(bytesConstRef());
    EmptyTrie = sm3(rlp(""));
    SignatureFromRLP = SM2SignatureFromRLP;
    SignatureFromBytes = SM2SignatureFromBytes;
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::SM4Encrypt);
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::SM4Decrypt);
    Sign = sm2Sign;
    Verify = sm2Verify;
    Recover = sm2Recover;
}
