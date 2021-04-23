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
/** @file SDFSM2Signature.cpp
 * @author maggiewu
 * @date 2021-02-01
 */
#include "SDFSM2Signature.h"
#include "SDFCryptoProvider.h"
#include "csmsds.h"
#include "libdevcore/Common.h"
#include "libdevcore/FixedHash.h"
#include "libdevcrypto/Common.h"
#include "libdevcrypto/SM2Signature.h"
#include "libdevcrypto/sm2/sm2.h"

using namespace std;
using namespace dev;
using namespace dev::crypto;

std::shared_ptr<crypto::Signature> dev::crypto::SDFSM2Sign(
    KeyPair const& _keyPair, const h256& _hash)
{
    SDFCryptoProvider& provider = SDFCryptoProvider::GetInstance();
    unsigned char signature[64];
    unsigned int signLen;
    Key key = Key();
    key.setPrivateKey((unsigned char*)_keyPair.secret().ref().data(), 32);
    key.setPublicKey((unsigned char*)_keyPair.pub().ref().data(), 64);
    h256 privk((byte const*)key.PrivateKey(),
        FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);

    // According to the SM2 standard
    // step 1 : calculate M' = Za || M
    // step 2 : e = H(M')
    // step 3 : signature = Sign(e)
    // get provider

    // Get Z
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen = SM3_DIGEST_LENGTH;
    std::string pubHex = toHex(_keyPair.pub().ref().data(), _keyPair.pub().ref().data() + 64, "04");
    bool getZ = SM2::sm2GetZFromPublicKey(pubHex,zValue,zValueLen);
    if(!getZ){
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of compute z" << LOG_KV("pubKey", pubHex);
        return nullptr;
    }

    // step 2 : e = H(M')
    unsigned char hashResult[SM3_DIGEST_LENGTH];
    unsigned int uiHashResultLen;
    unsigned int code = provider.HashWithZ(nullptr, SM3, zValue, zValueLen, _hash.data(),
        SM3_DIGEST_LENGTH, (unsigned char*)hashResult, &uiHashResultLen);
    if (code != SDR_OK)
    {
        throw provider.GetErrorMessage(code);
    }

    // step 3 : signature = Sign(e)
    code = provider.Sign(key, SM2, (const unsigned char*)hashResult, 32, signature, &signLen);
    if (code != SDR_OK)
    {
        throw provider.GetErrorMessage(code);
    }
    h256 r((byte const*)signature, FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);
    h256 s((byte const*)(signature + 32),
        FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);
    return make_shared<SM2Signature>(r, s, _keyPair.pub());
}

bool dev::crypto::SDFSM2Verify(
    h512 const& _pubKey, std::shared_ptr<crypto::Signature> _sig, const h256& _hash)
{
    // get provider
    SDFCryptoProvider& provider = SDFCryptoProvider::GetInstance();

    // parse input
    Key key = Key();
    key.setPublicKey((unsigned char*)_pubKey.ref().data(), 64);
    bool verifyResult = false;

    // Get Z
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen = SM3_DIGEST_LENGTH;
    std::string pubHex = toHex(_pubKey.data(), _pubKey.data() + 64, "04");
    bool getZ = SM2::sm2GetZFromPublicKey(pubHex,zValue,zValueLen);
    if(!getZ){
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of compute z" << LOG_KV("pubKey", pubHex);
        return false;
    }

    unsigned char hashResult[SM3_DIGEST_LENGTH];
    unsigned int uiHashResultLen;
    unsigned int code = provider.HashWithZ(nullptr, SM3, zValue, zValueLen, _hash.data(),
        SM3_DIGEST_LENGTH, (unsigned char*)hashResult, &uiHashResultLen);
    if (code != SDR_OK)
    {
        throw provider.GetErrorMessage(code);
    }

    code = provider.Verify(
        key, SM2, (const unsigned char*)hashResult, 32, _sig->asBytes().data(), 64, &verifyResult);

    if (code == SDR_OK)
    {
        return true;
    }
    else if (code == SDR_VERIFYERR)
    {
        return false;
    }
    else
    {
        throw provider.GetErrorMessage(code);
    }
}

h512 dev::crypto::SDFSM2Recover(std::shared_ptr<crypto::Signature> _s, const h256& _message)
{
    auto _sig = dynamic_pointer_cast<SM2Signature>(_s);
    if (!_sig)
    {
        return h512{};
    }

    if (!_sig->isValid())
    {
        return h512{};
    }
    if (SDFSM2Verify(_sig->v, _sig, _message))
    {
        return _sig->v;
    }
    return h512{};
}