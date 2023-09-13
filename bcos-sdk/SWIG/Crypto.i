%{
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-crypto/interfaces/crypto/KeyFactory.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/hash/SM3.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-crypto/interfaces/crypto/Signature.h"
#include "bcos-crypto/signature/fastsm2/FastSM2Crypto.h"
#include "bcos-crypto/signature/key/KeyFactoryImpl.h"
#include "bcos-crypto/encrypt/AESCrypto.h"
#include "bcos-crypto/encrypt/SM4Crypto.h"

using namespace bcos::crypto;

inline std::shared_ptr<bcos::crypto::CryptoSuite> newCryptoSuite(bool sm) {
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite;
    if (sm) {
        cryptoSuite = std::make_shared<CryptoSuite>(std::make_shared<SM3>(), std::make_shared<FastSM2Crypto>(), std::make_shared<SM4Crypto>());
    }
    else {
        cryptoSuite =  std::make_shared<CryptoSuite>(std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), std::make_shared<AESCrypto>());
    }
    cryptoSuite->setKeyFactory(std::make_shared<bcos::crypto::KeyFactoryImpl>());
    return cryptoSuite;
}
inline const bcos::crypto::KeyPairInterface& pointerToReference(const bcos::crypto::KeyPairInterface::UniquePtr& ptr) {
    return *ptr;
}
inline const bcos::crypto::KeyPairInterface& pointerToReference(const std::shared_ptr<bcos::crypto::KeyPairInterface>& ptr) {
    return *ptr;
}
%}

%include <stdint.i>
%include <std_shared_ptr.i>
%shared_ptr(bcos::crypto::CryptoSuite)
%shared_ptr(bcos::crypto::KeyFactory)
%shared_ptr(bcos::crypto::SignatureCrypto)

%include "../bcos-crypto/bcos-crypto/interfaces/crypto/KeyPairInterface.h"
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/KeyFactory.h"
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/Signature.h"
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/CryptoSuite.h"
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/Hash.h"
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/SymmetricEncryption.h"
%include "../bcos-crypto/bcos-crypto/signature/fastsm2/FastSM2Crypto.h"
%include "../bcos-crypto/bcos-crypto/encrypt/AESCrypto.h"
%include "../bcos-crypto/bcos-crypto/encrypt/SM4Crypto.h"
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/CommonType.h"

inline std::shared_ptr<bcos::crypto::CryptoSuite> newCryptoSuite(bool sm);
inline const bcos::crypto::KeyPairInterface& pointerToReference(const bcos::crypto::KeyPairInterface::UniquePtr& ptr);
inline const bcos::crypto::KeyPairInterface& pointerToReference(const std::shared_ptr<bcos::crypto::KeyPairInterface>& ptr);

