%{
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/hash/Keccak256.h"

using namespace bcos::crypto;

inline bcos::crypto::CryptoSuite::Ptr newCryptoSuite() {
    return std::make_shared<CryptoSuite>(std::make_shared<Keccak256>(), nullptr, nullptr);
}
%}

%include <stdint.i>
%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%shared_ptr(bcos::crypto::CryptoSuite)
%shared_ptr(bcos::crypto::Hash)
%include "../bcos-crypto/bcos-crypto/interfaces/crypto/CryptoSuite.h"

inline bcos::crypto::CryptoSuite::Ptr newCryptoSuite();