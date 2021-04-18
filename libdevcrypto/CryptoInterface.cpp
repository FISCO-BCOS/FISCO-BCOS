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
#include "libdevcore/Log.h"
#include "libdevcore/RLP.h"
#ifndef SDF
#include "sdf/SDFSM2Signature.h"
#include "sdf/SDFSM3Hash.h"
#include "sdf/SDFSM4Crypto.h"
#endif
#include <libconfig/GlobalConfigure.h>
#define CRYPTO_LOG(LEVEL) LOG(LEVEL) << "[CRYPTO] "


using namespace std;
using namespace dev;
using namespace dev::crypto;

h256 dev::EmptyHash = keccak256(bytesConstRef());
h256 dev::EmptyTrie = keccak256(rlp(""));

std::function<std::string(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::aesCBCEncrypt);
std::function<std::string(const unsigned char* _encryptedData, size_t _encryptedDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::aesCBCDecrypt);
std::function<std::shared_ptr<crypto::Signature>(RLP const& _rlp, size_t _start)>
    dev::crypto::SignatureFromRLP = ecdsaSignatureFromRLP;
std::function<std::shared_ptr<crypto::Signature>(std::vector<unsigned char>)>
    dev::crypto::SignatureFromBytes = ecdsaSignatureFromBytes;

std::function<std::shared_ptr<crypto::Signature>(KeyPair const& _keyPair, const h256& _hash)>
    dev::crypto::Sign = ecdsaSign;
std::function<bool(h512 const& _pubKey, std::shared_ptr<crypto::Signature> _sig, const h256& _hash)>
    dev::crypto::Verify = ecdsaVerify;
std::function<h512(std::shared_ptr<crypto::Signature> _sig, const h256& _hash)>
    dev::crypto::Recover = ecdsaRecover;

size_t dev::crypto::signatureLength()
{
    if (g_BCOSConfig.SMCrypto())
    {
        // SM2SignatureLength;
        return 128;
    }
    // ECDSASignatureLength;
    return 65;
}

void dev::crypto::initSMCrypto()
{
    EmptyHash = sm3(bytesConstRef());
    EmptyTrie = sm3(rlp(""));
    SignatureFromRLP = sm2SignatureFromRLP;
    SignatureFromBytes = sm2SignatureFromBytes;
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::sm4Encrypt);
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::sm4Decrypt);
    Sign = sm2Sign;
    Verify = sm2Verify;
    Recover = sm2Recover;
}
#ifndef SDF
void dev::crypto::initHsmSMCrypto()
{
    CRYPTO_LOG(INFO) << "[CryptoInterface:initHsmSMCrypto] use hardware secure module";
    EmptyHash = SDFSM3(bytesConstRef());
    EmptyTrie = SDFSM3(rlp(""));
    SignatureFromRLP = sm2SignatureFromRLP;
    SignatureFromBytes = sm2SignatureFromBytes;
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::SDFSM4Encrypt);
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::SDFSM4Decrypt);
    Sign = SDFSM2Sign;
    Verify = SDFSM2Verify;
    Recover = SDFSM2Recover;
}
#endif
void dev::crypto::initCrypto()
{
    EmptyHash = keccak256(bytesConstRef());
    EmptyTrie = keccak256(rlp(""));
    SignatureFromRLP = ecdsaSignatureFromRLP;
    SignatureFromBytes = ecdsaSignatureFromBytes;
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::aesCBCEncrypt);
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::crypto::aesCBCDecrypt);
    Sign = ecdsaSign;
    Verify = ecdsaVerify;
    Recover = ecdsaRecover;
}
