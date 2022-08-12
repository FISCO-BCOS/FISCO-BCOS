#pragma once
#include "OpenSSLHasher.h"
#include <variant>

namespace bcos::crypto::hasher
{

// Type erasure hasher
using AnyHasher = std::variant<openssl::OpenSSL_SHA3_256_Hasher, openssl::OpenSSL_SHA2_256_Hasher,
    openssl::OpenSSL_SM3_Hasher, openssl::OpenSSL_Keccak256_Hasher>;

}  // namespace bcos::crypto::hasher