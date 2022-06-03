#pragma once

#include "Hasher.h"
#include <openssl/evp.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
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
class OpenSSLHasher : public HasherBase<OpenSSLHasher<hasherType>>
{
public:
    OpenSSLHasher()
      : HasherBase<OpenSSLHasher<hasherType>>(), m_mdCtx(EVP_MD_CTX_new()), m_init(false)
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
    ~OpenSSLHasher() override = default;

    void impl_update(std::span<std::byte const> in)
    {
        if (!m_init)
        {
            init();
            m_init = true;
        }
        if (!EVP_DigestUpdate(m_mdCtx.get(), in.data(), in.size())) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestUpdate error!"});
        }
    }

    void impl_final(std::span<std::byte> out)
    {
        m_init = false;
        if (out.size() < HASH_SIZE) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});
        }
        if (!EVP_DigestFinal(m_mdCtx.get(), reinterpret_cast<unsigned char*>(out.data()), nullptr))
            [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestFinal error!"});
        }
    }

    constexpr static size_t impl_hashSize() noexcept { return HASH_SIZE; }


private:
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

using OpenSSL_SHA3_256_Hasher = OpenSSLHasher<SHA3_256>;
using OpenSSL_SHA2_256_Hasher = OpenSSLHasher<SHA2_256>;
using OPENSSL_SM3_Hasher = OpenSSLHasher<SM3_256>;

static_assert(Hasher<OpenSSL_SHA3_256_Hasher>, "Assert OpenSSLHasher type");
static_assert(Hasher<OpenSSL_SHA2_256_Hasher>, "Assert OpenSSLHasher type");
static_assert(Hasher<OPENSSL_SM3_Hasher>, "Assert OpenSSLHasher type");

}  // namespace bcos::crypto::hasher::openssl
