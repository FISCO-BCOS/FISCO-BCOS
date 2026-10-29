#pragma once

#include <openssl/ssl.h>

namespace bcos::boostssl::context
{
void enableLegacyNtlsChain(SSL_CTX* context);
int verifyLegacyNtlsChain(X509_STORE_CTX* context, void* arg);
}  // namespace bcos::boostssl::context
