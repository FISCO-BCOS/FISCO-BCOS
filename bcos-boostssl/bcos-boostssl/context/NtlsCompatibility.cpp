#include <bcos-boostssl/context/NtlsCompatibility.h>
#include <openssl/x509v3.h>
#include <cstdint>
#include <memory>
#include <mutex>

namespace bcos::boostssl::context
{
namespace
{
constexpr uint32_t SIGN_USAGE = X509v3_KU_DIGITAL_SIGNATURE | X509v3_KU_NON_REPUDIATION;
constexpr uint32_t ENC_USAGE =
    X509v3_KU_KEY_ENCIPHERMENT | X509v3_KU_DATA_ENCIPHERMENT | X509v3_KU_KEY_AGREEMENT;

using StoreContext = std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)>;
struct StackDeleter
{
    void operator()(STACK_OF(X509)* stack) const { sk_X509_free(stack); }
};
using CertificateStack = std::unique_ptr<STACK_OF(X509), StackDeleter>;

struct CertificateRoles
{
    X509* sign = nullptr;
    X509* enc = nullptr;
};

int stateIndex()
{
    static std::once_flag once;
    static int index = -1;
    std::call_once(once, []() {
        index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    });
    return index;
}

int fail(X509_STORE_CTX* context, int error)
{
    X509_STORE_CTX_set_error(context, error);
    return 0;
}

bool findRoles(STACK_OF(X509)* chain, CertificateRoles& roles)
{
    size_t endEntityCount = 0;
    for (int i = 0; i < sk_X509_num(chain); ++i)
    {
        auto* cert = sk_X509_value(chain, i);
        if (X509_check_ca(cert) > 0)
        {
            continue;
        }
        ++endEntityCount;
        const auto usage = X509_get_key_usage(cert);
        if (usage == UINT32_MAX)
        {
            return false;
        }
        if ((usage & SIGN_USAGE) != 0)
        {
            if (roles.sign)
            {
                return false;
            }
            roles.sign = cert;
        }
        if ((usage & ENC_USAGE) != 0)
        {
            if (roles.enc)
            {
                return false;
            }
            roles.enc = cert;
        }
    }
    return endEntityCount == 2 && roles.sign && roles.enc && roles.sign != roles.enc;
}

bool originalOrderSwapped(STACK_OF(X509)* chain, const CertificateRoles& roles)
{
    // Undo Tongsuo's first left rotation to recover the order sent by the peer.
    const auto count = sk_X509_num(chain);
    int signIndex = -1;
    int encIndex = -1;
    for (int i = 0; i < count; ++i)
    {
        auto* cert = sk_X509_value(chain, i);
        if (cert == roles.sign)
        {
            signIndex = (i + 1) % count;
        }
        else if (cert == roles.enc)
        {
            encIndex = (i + 1) % count;
        }
    }
    return signIndex > encIndex;
}

bool verifyCertificate(X509_STORE_CTX* context, SSL* ssl, X509* leaf)
{
    CertificateStack chain(sk_X509_new_null());
    if (!chain || sk_X509_push(chain.get(), leaf) == 0)
    {
        return false;
    }
    auto* untrusted = X509_STORE_CTX_get0_untrusted(context);
    for (int i = 0; i < sk_X509_num(untrusted); ++i)
    {
        auto* cert = sk_X509_value(untrusted, i);
        if (X509_check_ca(cert) > 0 && sk_X509_push(chain.get(), cert) == 0)
        {
            return false;
        }
    }

    StoreContext verifyContext(X509_STORE_CTX_new(), &X509_STORE_CTX_free);
    if (!verifyContext ||
        X509_STORE_CTX_init(verifyContext.get(), X509_STORE_CTX_get0_store(context), leaf,
            chain.get()) != 1 ||
        X509_STORE_CTX_set_ex_data(verifyContext.get(), SSL_get_ex_data_X509_STORE_CTX_idx(), ssl) !=
            1 ||
        X509_VERIFY_PARAM_set1(X509_STORE_CTX_get0_param(verifyContext.get()),
            X509_STORE_CTX_get0_param(context)) != 1)
    {
        return false;
    }
    if (auto callback = SSL_get_verify_callback(ssl))
    {
        X509_STORE_CTX_set_verify_cb(verifyContext.get(), callback);
    }
    if (X509_verify_cert(verifyContext.get()) <= 0)
    {
        X509_STORE_CTX_set_error(context, X509_STORE_CTX_get_error(verifyContext.get()));
        X509_STORE_CTX_set_error_depth(context, X509_STORE_CTX_get_error_depth(verifyContext.get()));
        X509_STORE_CTX_set_current_cert(
            context, X509_STORE_CTX_get_current_cert(verifyContext.get()));
        return false;
    }
    auto* verifiedChain = X509_STORE_CTX_get1_chain(verifyContext.get());
    if (!verifiedChain)
    {
        return false;
    }
    X509_STORE_CTX_set0_verified_chain(context, verifiedChain);
    return true;
}

bool reorderCertificates(STACK_OF(X509)* chain, X509* first, X509* second)
{
    CertificateStack reordered(sk_X509_new_null());
    if (!reordered || sk_X509_push(reordered.get(), first) == 0 ||
        sk_X509_push(reordered.get(), second) == 0)
    {
        return false;
    }
    for (int i = 0; i < sk_X509_num(chain); ++i)
    {
        auto* cert = sk_X509_value(chain, i);
        if (cert != first && cert != second && sk_X509_push(reordered.get(), cert) == 0)
        {
            return false;
        }
    }
    for (int i = 0; i < sk_X509_num(chain); ++i)
    {
        sk_X509_set(chain, i, sk_X509_value(reordered.get(), i));
    }
    return true;
}
}  // namespace

void enableLegacyNtlsChain(SSL_CTX* context)
{
    if (context)
    {
        SSL_CTX_set_cert_verify_callback(context, verifyLegacyNtlsChain, nullptr);
    }
}

int verifyLegacyNtlsChain(X509_STORE_CTX* context, void*)
{
    auto* ssl = static_cast<SSL*>(
        X509_STORE_CTX_get_ex_data(context, SSL_get_ex_data_X509_STORE_CTX_idx()));
    auto* chain = X509_STORE_CTX_get0_untrusted(context);
    const auto index = stateIndex();
    if (!ssl || !chain || index < 0)
    {
        return fail(context, X509_V_ERR_INVALID_PURPOSE);
    }

    CertificateRoles roles;
    if (!findRoles(chain, roles))
    {
        return fail(context, X509_V_ERR_INVALID_PURPOSE);
    }
    const auto state = reinterpret_cast<uintptr_t>(SSL_get_ex_data(ssl, index));
    const bool second = state != 0;
    const bool swapped = second ? state == 2 : originalOrderSwapped(chain, roles);
    if (!verifyCertificate(context, ssl, second ? roles.sign : roles.enc))
    {
        return 0;
    }

    if (!second)
    {
        if (SSL_set_ex_data(ssl, index, reinterpret_cast<void*>(swapped ? 2 : 1)) != 1)
        {
            return fail(context, X509_V_ERR_OUT_OF_MEM);
        }
        return 1;
    }

    if (!reorderCertificates(
            chain, swapped ? roles.enc : roles.sign, swapped ? roles.sign : roles.enc))
    {
        return fail(context, X509_V_ERR_OUT_OF_MEM);
    }
    if (SSL_set_ex_data(ssl, index, nullptr) != 1)
    {
        return fail(context, X509_V_ERR_OUT_OF_MEM);
    }
    return 1;
}
}  // namespace bcos::boostssl::context
