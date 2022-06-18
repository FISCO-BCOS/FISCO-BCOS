#pragma once

#include "../TrivialObject.h"
#include "Hasher.h"
#include <openssl/evp.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace bcos::crypto::hasher::openssl
{
enum HasherType
{
    SM3,
    SHA3_256,
    SHA2_256,
    Keccak256
};

template <HasherType hasherType>
class OpenSSLHasher
{
public:
    OpenSSLHasher() : m_mdCtx(EVP_MD_CTX_new()), m_init(false)
    {
        if (!m_mdCtx) [[unlikely]] { BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_MD_CTX_new error!"}); }
    }

    OpenSSLHasher(const OpenSSLHasher&) = delete;
    OpenSSLHasher(OpenSSLHasher&&) = default;
    OpenSSLHasher& operator=(const OpenSSLHasher&) = delete;
    OpenSSLHasher& operator=(OpenSSLHasher&&) = default;
    ~OpenSSLHasher() = default;

    constexpr static size_t HASH_SIZE = 32;

    void init()
    {
        auto md = chooseMD();

        if (!EVP_DigestInit(m_mdCtx.get(), md)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestInit error!"});
        }

        // Keccak256 special padding
        if constexpr (hasherType == Keccak256)
        {
            struct KECCAK1600_CTX
            {
                uint64_t A[5][5];
                size_t block_size;
                size_t md_size;
                size_t num;
                unsigned char buf[1600 / 8 - 32];
                unsigned char pad;
            };

            struct EVP_MD_CTX_Keccak256
            {
                const EVP_MD* digest;
                ENGINE* engine;
                unsigned long flags;

                KECCAK1600_CTX* md_data;
            };

            auto keccak256 = reinterpret_cast<EVP_MD_CTX_Keccak256*>(m_mdCtx.get());
            if (!keccak256->md_data || keccak256->md_data->pad != 0x06)  // The sha3 origin pad
            {
                BOOST_THROW_EXCEPTION(
                    std::runtime_error{"OpenSSL KECCAK1600_CTX layout error! Maybe untested openssl version"});
            }
            keccak256->md_data->pad = 0x01;
        }
    }

    void update(bcos::crypto::trivial::Object auto&& in)
    {
        if (!m_init)
        {
            init();
            m_init = true;
        }
        auto view = bcos::crypto::trivial::toView(std::forward<decltype(in)>(in));

        if (!EVP_DigestUpdate(m_mdCtx.get(), view.data(), view.size())) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestUpdate error!"});
        }
    }

    void final(bcos::crypto::trivial::Object auto&& out)
    {
        m_init = false;
        if (out.size() < HASH_SIZE) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});
        }
        auto view = bcos::crypto::trivial::toView(std::forward<decltype(out)>(out));

        if (!EVP_DigestFinal(m_mdCtx.get(), reinterpret_cast<unsigned char*>(view.data()), nullptr)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_DigestFinal error!"});
        }
    }

    constexpr const EVP_MD* chooseMD()
    {
        if constexpr (hasherType == SM3) { return EVP_sm3(); }
        else if constexpr (hasherType == SHA3_256 || hasherType == Keccak256) { return EVP_sha3_256(); }
        else if constexpr (hasherType == SHA2_256) { return EVP_sha256(); }
        else { static_assert(!sizeof(*this), "Unknown EVP Type!"); }
    }

    struct Deleter
    {
        void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); }
    };

    std::unique_ptr<EVP_MD_CTX, Deleter> m_mdCtx;
    bool m_init;
};

using OpenSSL_SHA3_256_Hasher = OpenSSLHasher<SHA3_256>;
using OpenSSL_SHA2_256_Hasher = OpenSSLHasher<SHA2_256>;
using OpenSSL_SM3_Hasher = OpenSSLHasher<SM3>;
using OpenSSL_Keccak256_Hasher = OpenSSLHasher<Keccak256>;

static_assert(Hasher<OpenSSL_SHA3_256_Hasher>, "Assert OpenSSLHasher type");
static_assert(Hasher<OpenSSL_SHA2_256_Hasher>, "Assert OpenSSLHasher type");
static_assert(Hasher<OpenSSL_SM3_Hasher>, "Assert OpenSSLHasher type");
static_assert(Hasher<OpenSSL_Keccak256_Hasher>, "Assert OpenSSLHasher type");

}  // namespace bcos::crypto::hasher::openssl
