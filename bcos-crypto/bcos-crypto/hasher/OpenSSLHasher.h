#pragma once

#include "../Concepts.h"
#include <openssl/evp.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <span>
#include <stdexcept>

namespace bcos::crypto::hasher::openssl
{
enum HasherType
{
    SM3_256,
    SHA3_256,
    SHA2_256
};

template <HasherType hasherType>
class OpenSSLHasher
{
public:
    OpenSSLHasher() : m_mdCtx(EVP_MD_CTX_new()), m_init(false)
    {
        if (!m_mdCtx) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_MD_CTX_new error!"});
        }
    }

    OpenSSLHasher(const OpenSSLHasher&) = delete;
    OpenSSLHasher(OpenSSLHasher&&) = default;
    OpenSSLHasher& operator=(const OpenSSLHasher&) = delete;
    OpenSSLHasher& operator=(OpenSSLHasher&&) = default;
    ~OpenSSLHasher() = default;

    void init()
    {
        auto md = chooseMD();

        if (!EVP_DigestInit(m_mdCtx.get(), md)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestInit error!"});
        }
    }

    constexpr const EVP_MD* chooseMD()
    {
        if constexpr (hasherType == SM3_256)
        {
            return EVP_sm3();
        }
        else if constexpr (hasherType == SHA3_256)
        {
            return EVP_sha3_256();
        }
        else if constexpr (hasherType == SHA2_256)
        {
            return EVP_sha256();
        }
        else
        {
            static_assert(!sizeof(*this), "Unknown EVP Type!");
        }
    }

    struct Deleter
    {
        void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); }
    };

    std::unique_ptr<EVP_MD_CTX, Deleter> m_mdCtx;
    bool m_init;

    constexpr static size_t HASH_SIZE = 32;
};

template <bcos::crypto::hasher::openssl::HasherType type>
void update(OpenSSLHasher<type>& hasher, bcos::crypto::trivial::Object auto&& in)
{
    if (!hasher.m_init)
    {
        hasher.init();
        hasher.m_init = true;
    }
    auto view = bcos::crypto::trivial::toView(std::forward<decltype(in)>(in));

    if (!EVP_DigestUpdate(hasher.m_mdCtx.get(), view.data(), view.size())) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestUpdate error!"});
    }
}

template <bcos::crypto::hasher::openssl::HasherType type>
void final(OpenSSLHasher<type>& hasher, bcos::crypto::trivial::Object auto&& out)
{
    hasher.m_init = false;
    if (out.size() < OpenSSLHasher<type>::HASH_SIZE) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});
    }
    auto view = bcos::crypto::trivial::toView(std::forward<decltype(out)>(out));

    if (!EVP_DigestFinal(hasher.m_mdCtx.get(), reinterpret_cast<unsigned char*>(view.data()),
            nullptr)) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestFinal error!"});
    }
}

using OpenSSL_SHA3_256_Hasher = OpenSSLHasher<SHA3_256>;
using OpenSSL_SHA2_256_Hasher = OpenSSLHasher<SHA2_256>;
using OPENSSL_SM3_Hasher = OpenSSLHasher<SM3_256>;
}  // namespace bcos::crypto::hasher::openssl

#include "Hasher.h"

namespace bcos::crypto::hasher
{

// using bcos::crypto::hasher::openssl::final;
// using bcos::crypto::hasher::openssl::update;

static_assert(
    Hasher<bcos::crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher>, "Assert OpenSSLHasher type");
static_assert(
    Hasher<bcos::crypto::hasher::openssl::OpenSSL_SHA2_256_Hasher>, "Assert OpenSSLHasher type");
static_assert(
    Hasher<bcos::crypto::hasher::openssl::OPENSSL_SM3_Hasher>, "Assert OpenSSLHasher type");
}  // namespace bcos::crypto::hasher
