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
#include "HSMSignature.h"
#include "CryptoProvider.h"
#include "csmsds.h"
#include "libdevcore/Common.h"
#include "libdevcore/FixedHash.h"
#include "libdevcrypto/Common.h"
#include "libdevcrypto/SM2Signature.h"
#include "libdevcrypto/sm2/sm2.h"
#include "sdf/SDFCryptoProvider.h"
#include <memory>
#include <vector>


using namespace std;
using namespace dev;
using namespace dev::crypto;
#if FISCO_SDF
using namespace hsm;
using namespace hsm::sdf;
#endif

std::shared_ptr<crypto::Signature> dev::crypto::SDFSM2Sign(
    KeyPair const& _keyPair, const h256& _hash)
{
    CryptoProvider& provider = SDFCryptoProvider::GetInstance();
    Key key = Key();
    if(_keyPair.isInternalKey()==true){
    	key = Key((_keyPair.keyIndex()+1)/2, NULL);
        CRYPTO_LOG(DEBUG)<<"[HSMSignature::key] is internal key "<< LOG_KV("keyIndex", key.identifier());
    }else{
    std::shared_ptr<const vector<byte>> privKey = std::make_shared<const std::vector<byte>>(
         (byte*)_keyPair.secret().data(), (byte*)_keyPair.secret().data() + 32);
    key.setPrivateKey(privKey);
	}
    std::vector<byte> signature(64);

    // According to the SM2 standard
    // step 1 : calculate M' = Za || M
    // step 2 : e = H(M')
    // step 3 : signature = Sign(e)
    // get provider

    // Get Z
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen = SM3_DIGEST_LENGTH;

    CRYPTO_LOG(DEBUG) << "[HSMSignature::pubHex] _keyPair = " << _keyPair.pub().hex();
    std::string pubHex = toHex(_keyPair.pub().ref().data(), _keyPair.pub().ref().data() + 64, "04");
    bool getZ = SM2::sm2GetZFromPublicKey(pubHex, zValue, zValueLen);
    if (!getZ)
    {
        CRYPTO_LOG(ERROR) << "[HSMSignature::sign] ERROR of compute z" << LOG_KV("pubKey", pubHex);
        return nullptr;
    }

    // step 2 : e = H(M')
    unsigned char hashResult[SM3_DIGEST_LENGTH];
    unsigned int uiHashResultLen;
    unsigned int code = provider.HashWithZ(nullptr, hsm::SM3, zValue, zValueLen, _hash.data(),
        SM3_DIGEST_LENGTH, (unsigned char*)hashResult, &uiHashResultLen);
    if (code != SDR_OK)
    {
        CRYPTO_LOG(ERROR) << "[HSMSignature::sign] ERROR of compute H(M')" << LOG_KV("error", provider.GetErrorMessage(code));
        return nullptr;
    }

    // step 3 : signature = Sign(e)
    unsigned int signLen;
    code = provider.Sign(
        key, hsm::SM2, (const unsigned char*)hashResult, 32, signature.data(), &signLen);
    if (code != SDR_OK)
    {
        CRYPTO_LOG(ERROR) << "[HSMSignature::sign] ERROR of sign" << LOG_KV("error", provider.GetErrorMessage(code));
        return nullptr;
    }
    //cout<<"hash "<< toHex(hashResult)<<endl;
    cout<<"Signature = "<< toHex(signature.data(),signature.data()+64,"")<<endl;
    h256 r((byte const*)signature.data(),
        FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);
    h256 s((byte const*)(signature.data() + 32),
        FixedHash<32>::ConstructFromPointerType::ConstructFromPointer);
    return make_shared<SM2Signature>(r, s, _keyPair.pub());
}

bool dev::crypto::SDFSM2Verify(
    h512 const& _pubKey, std::shared_ptr<crypto::Signature> _sig, const h256& _hash)
{
    // get provider
    CryptoProvider& provider = SDFCryptoProvider::GetInstance();

    // parse input
    Key key = Key();
    std::shared_ptr<const vector<byte>> pubKey = std::make_shared<const std::vector<byte>>(
        (byte*)_pubKey.ref().data(), (byte*)_pubKey.ref().data() + 64);
    key.setPublicKey(pubKey);
    bool verifyResult = false;

    // Get Z
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen = SM3_DIGEST_LENGTH;
    std::string pubHex = toHex(_pubKey.data(), _pubKey.data() + 64, "04");
    bool getZ = SM2::sm2GetZFromPublicKey(pubHex, zValue, zValueLen);
    if (!getZ)
    {
        CRYPTO_LOG(ERROR) << "[HSMSignature::veify] ERROR of compute z" << LOG_KV("pubKey", pubHex);
        return false;
    }

    vector<byte> hashResult(SM3_DIGEST_LENGTH);
    unsigned int uiHashResultLen;
    unsigned int code = provider.HashWithZ(nullptr, hsm::SM3, zValue, zValueLen, _hash.data(),
        SM3_DIGEST_LENGTH, (unsigned char*)hashResult.data(), &uiHashResultLen);
    if (code != SDR_OK)
    {
        throw provider.GetErrorMessage(code);
    }

    code = provider.Verify(key, hsm::SM2, (const unsigned char*)hashResult.data(),
        SM3_DIGEST_LENGTH, _sig->asBytes().data(), 64, &verifyResult);

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
