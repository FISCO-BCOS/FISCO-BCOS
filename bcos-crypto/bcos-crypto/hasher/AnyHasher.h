#pragma once
#include "OpenSSLHasher.h"
#include <variant>

namespace bcos::crypto::hasher
{

// Type erasure hasher
class AnyHasher
{
public:
    AnyHasher(bcos::crypto::hasher::Hasher auto hasher) : m_hasher(std::move(hasher)) {}

    void update(bcos::crypto::trivial::Object auto&& in)
    {
        std::visit([&in](auto& hasher) { hasher.update(in); }, m_hasher);
    };

    void final(bcos::crypto::trivial::Object auto&& out)
    {
        std::visit([&out](auto& hasher) { hasher.final(out); }, m_hasher);
    }

private:
    std::variant<openssl::OpenSSL_SHA3_256_Hasher, openssl::OpenSSL_SHA2_256_Hasher,
        openssl::OpenSSL_SM3_Hasher, openssl::OpenSSL_Keccak256_Hasher>
        m_hasher;
};
}  // namespace bcos::crypto::hasher