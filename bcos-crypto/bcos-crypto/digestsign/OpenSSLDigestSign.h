#pragma once

#include "DigestSign.h"
#include <openssl/evp.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <stdexcept>

namespace bcos::crypto::digestsign::openssl
{

enum DigestSignType
{
    SM2
};

template <DigestSignType digestSignType>
class OpenSSLDigestSign
{
private:
    constexpr static int id()
    {
        if constexpr (digestSignType == SM2) { return EVP_PKEY_SM2; }
        else { static_assert(!sizeof(OpenSSLDigestSign), "Unknown DigestSignType!"); }
    }

    struct EVPContext
    {
        EVPContext() : m_context(EVP_PKEY_CTX_new_id(id(), nullptr)) {}

        EVP_PKEY_CTX* context() { return m_context.get(); }
        struct Deleter
        {
            void operator()(EVP_PKEY_CTX* p) const { EVP_PKEY_CTX_free(p); }
        };
        std::unique_ptr<EVP_PKEY_CTX, Deleter> m_context;
    };

public:
    constexpr static size_t KEY_SIZE = 32;
    constexpr static size_t SIGN_SIZE = 32;

    class Key
    {
    public:
        Key() : m_key{EVP_PKEY_new()} {};

        Key(const Key&) = delete;
        Key(Key&&) noexcept = default;
        Key& operator=(const Key&) = delete;
        Key& operator=(Key&&) noexcept = default;
        ~Key() = default;

    private:
        Key(EVP_PKEY* pkey) : m_key{pkey} {};

        const EVP_PKEY* pkey() const { return m_key.get(); }
        EVP_PKEY* pkey() { return m_key.get(); }

        void setPKey(EVP_PKEY* pkey) { m_key.reset(pkey); }

        struct Deleter
        {
            void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
        };
        std::unique_ptr<EVP_PKEY, Deleter> m_key;
    };

    struct Sign : public std::array<std::byte, SIGN_SIZE>
    {
        using std::array<std::byte, SIGN_SIZE>::array;
    };

    class KeyGenerator
    {
    public:
        void gen(Key& key)
        {
            if (!EVP_PKEY_keygen_init(m_context.context())) [[unlikely]]
                BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_PKEY_keygen_init error!"});

            EVP_PKEY** ppkey = nullptr;
            if (!EVP_PKEY_keygen(m_context.context(), ppkey)) [[unlikely]]
                BOOST_THROW_EXCEPTION(std::runtime_error{"EVP_PKEY_keygen error!"});

            key.setPKey(*ppkey);
        }

    private:
        EVPContext m_context;
    };

    class Encrypter
    {
    };
};

static_assert(DigestSign<OpenSSLDigest<SM2>>, "");
}  // namespace bcos::crypto::digestsign::openssl