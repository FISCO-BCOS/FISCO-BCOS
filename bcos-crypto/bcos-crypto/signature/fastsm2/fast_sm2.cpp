/**
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief implementation for ed25519 keyPair algorithm
 * @file fast_sm2.h
 * @date 2022.01.17
 * @author yujiechen
 */
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/fastsm2/fast_sm2.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/LogStream.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>

using namespace bcos;
using namespace bcos::crypto;

constexpr char c_userId[] = "1234567812345678";
const int c_R_FIELD_LEN = 32;
const int c_S_FIELD_LEN = 32;
const int c_PRIVATEKEY_LEN = 32;
const int c_PUBLICKEY_LEN = 64;
// Note: EC_GROUP_new_by_curve_name cost performance, So only one copy is created globally to
// improve performance
EC_GROUP* sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);

// C interface for 'fast_sm2_sign'.
int8_t bcos::crypto::fast_sm2_sign(const CInputBuffer* raw_private_key,
    const CInputBuffer* raw_public_key, const CInputBuffer* raw_message_hash,
    COutputBuffer* output_signature)
{
    int len = 0;
    EC_KEY* sm2Key = nullptr;
    ECDSA_SIG* sig = nullptr;
    EC_POINT* publicKey = nullptr;
    BIGNUM* privateKey = nullptr;
    BIGNUM* bn = nullptr;
    EVP_PKEY* evpKey = nullptr;
    EVP_PKEY_CTX* evpKeyCtx = nullptr;
    EVP_MD_CTX* mdCtx = nullptr;
    unsigned char* derSig = nullptr;
    const unsigned char* derRead = nullptr;
    size_t derSigLen = 0;
    int8_t ret = WEDPR_ERROR;
    if (raw_public_key->len != (size_t)c_PUBLICKEY_LEN)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("fast_sm2_sign: invalid publicKey length")
                            << LOG_KV("len", raw_public_key->len);
        return WEDPR_ERROR;
    }
    if (output_signature->len < (size_t)(c_R_FIELD_LEN + c_S_FIELD_LEN))
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("fast_sm2_sign: output buffer too small")
                            << LOG_KV("len", output_signature->len);
        return WEDPR_ERROR;
    }
    uint8_t publicKeyData[65] = {4};
    memcpy(publicKeyData + 1, raw_public_key->data, raw_public_key->len);
    bn = BN_bin2bn((const unsigned char*)publicKeyData, 65, NULL);
    if (bn == nullptr)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("fast_sm2_sign: error of BN_bin2bn for publicKey");
        goto done;
    }

    privateKey = BN_bin2bn((const unsigned char*)raw_private_key->data, raw_private_key->len, NULL);
    if (privateKey == nullptr)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("sm2: fast_sm2_sign: error of BN_bin2bn for privateKey");
        goto done;
    }

    publicKey = EC_POINT_bn2point(sm2Group, bn, NULL, NULL);
    if (publicKey == NULL)
    {
        CRYPTO_LOG(WARNING)
            << LOG_DESC("sm2: fast_sm2_sign: error of EC_POINT_bn2point for publicKey")
            << LOG_KV("len", raw_public_key->len)
            << LOG_KV("pubHex",
                   toHex(bytesConstRef((const byte*)raw_public_key->data, raw_public_key->len),
                       "0x"));
        goto done;
    }
    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error of EC_KEY_new";
        goto done;
    }
    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error of EC_KEY_set_group";
        goto done;
    }
    if (!EC_KEY_set_private_key(sm2Key, privateKey))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error of EC_KEY_set_private_key";
        goto done;
    }
    if (!EC_KEY_set_public_key(sm2Key, publicKey))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error of EC_KEY_set_public_key";
        goto done;
    }

    evpKey = EVP_PKEY_new();
    mdCtx = EVP_MD_CTX_new();
    if (!evpKey || !mdCtx || EVP_PKEY_set1_EC_KEY(evpKey, sm2Key) != 1)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error creating EVP key";
        goto done;
    }
    evpKeyCtx = EVP_PKEY_CTX_new_from_pkey(NULL, evpKey, NULL);
    if (!evpKeyCtx)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error creating EVP key context";
        goto done;
    }
    EVP_MD_CTX_set_pkey_ctx(mdCtx, evpKeyCtx);
    if (EVP_DigestSignInit(mdCtx, NULL, EVP_sm3(), NULL, evpKey) != 1 ||
        EVP_PKEY_CTX_set1_id(evpKeyCtx, c_userId, sizeof(c_userId) - 1) != 1 ||
        EVP_DigestSignUpdate(mdCtx, raw_message_hash->data, raw_message_hash->len) != 1 ||
        EVP_DigestSignFinal(mdCtx, NULL, &derSigLen) != 1)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error initializing EVP signature";
        goto done;
    }
    derSig = static_cast<unsigned char*>(OPENSSL_malloc(derSigLen));
    if (!derSig)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error allocating signature";
        goto done;
    }
    if (EVP_DigestSignFinal(mdCtx, derSig, &derSigLen) != 1)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error creating EVP signature";
        goto done;
    }
    derRead = derSig;
    sig = d2i_ECDSA_SIG(NULL, &derRead, static_cast<long>(derSigLen));
    if (!sig || derRead != derSig + derSigLen)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_sign: error decoding EVP signature";
        goto done;
    }
    len = BN_bn2bin(ECDSA_SIG_get0_r(sig), (unsigned char*)output_signature->data);
    if (len < c_R_FIELD_LEN)
    {
        memmove(output_signature->data + (c_R_FIELD_LEN - len), output_signature->data, len);
        memset(output_signature->data, 0, (c_R_FIELD_LEN - len));
    }
    len =
        BN_bn2bin(ECDSA_SIG_get0_s(sig), (unsigned char*)(output_signature->data + c_R_FIELD_LEN));
    if (len < c_S_FIELD_LEN)
    {
        auto startPointer = output_signature->data + c_R_FIELD_LEN;
        memmove(startPointer + (c_S_FIELD_LEN - len), startPointer, len);
        memset(startPointer, 0, (c_S_FIELD_LEN - len));
    }
    ret = WEDPR_SUCCESS;
done:
    if (mdCtx)
    {
        EVP_MD_CTX_free(mdCtx);
    }
    if (evpKeyCtx)
    {
        EVP_PKEY_CTX_free(evpKeyCtx);
    }
    if (evpKey)
    {
        EVP_PKEY_free(evpKey);
    }
    if (derSig)
    {
        OPENSSL_free(derSig);
    }
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    if (sig)
    {
        ECDSA_SIG_free(sig);
    }
    if (privateKey)
    {
        BN_free(privateKey);
    }
    if (publicKey)
    {
        EC_POINT_free(publicKey);
    }
    if (bn)
    {
        BN_free(bn);
    }
    return ret;
}

int8_t bcos::crypto::fast_sm2_verify(const CInputBuffer* raw_public_key,
    const CInputBuffer* raw_message_hash, const CInputBuffer* raw_signature)
{
    EC_KEY* sm2Key = NULL;
    EC_POINT* point = NULL;
    BIGNUM* r = NULL;
    BIGNUM* s = NULL;
    BIGNUM* bn = NULL;
    ECDSA_SIG* signData = NULL;
    EVP_PKEY* evpKey = nullptr;
    EVP_PKEY_CTX* evpKeyCtx = nullptr;
    EVP_MD_CTX* mdCtx = nullptr;
    unsigned char* derSig = nullptr;
    int derSigLen = 0;
    int8_t ret = WEDPR_ERROR;
    if (raw_public_key->len != (size_t)c_PUBLICKEY_LEN)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("fast_sm2_verify: invalid publicKey length")
                            << LOG_KV("len", raw_public_key->len);
        return WEDPR_ERROR;
    }
    if (raw_signature->len < (size_t)(c_R_FIELD_LEN + c_S_FIELD_LEN))
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("fast_sm2_verify: invalid signature length")
                            << LOG_KV("len", raw_signature->len);
        return WEDPR_ERROR;
    }
    uint8_t publicKeyData[65] = {4};
    memcpy(publicKeyData + 1, raw_public_key->data, raw_public_key->len);
    bn = BN_bin2bn((const unsigned char*)publicKeyData, 65, NULL);
    point = EC_POINT_new(sm2Group);
    if (point == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of EC_POINT_new";
        goto done;
    }

    if (!EC_POINT_bn2point(sm2Group, bn, point, NULL))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of EC_POINT_bin2point";
        goto done;
    }

    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of EC_KEY_new";
        goto done;
    }

    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of EC_KEY_set_group";
        goto done;
    }
    if (!EC_KEY_set_public_key(sm2Key, point))
    {
        CRYPTO_LOG(WARNING) << "EC_KEY_set_public_key of EC_KEY_set_public_key";
        goto done;
    }
    r = BN_bin2bn((const unsigned char*)raw_signature->data, c_R_FIELD_LEN, NULL);
    if (r == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of BN_bin2bn for r";
        goto done;
    }
    s = BN_bin2bn((const unsigned char*)(raw_signature->data + c_R_FIELD_LEN), c_S_FIELD_LEN, NULL);
    if (s == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of BN_bin2bn for s";
        goto done;
    }
    signData = ECDSA_SIG_new();
    if (signData == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of ECDSA_SIG_new";
        goto done;
    }
    if (!ECDSA_SIG_set0(signData, r, s))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error of ECDSA_SIG_set0";
        goto done;
    }

    evpKey = EVP_PKEY_new();
    mdCtx = EVP_MD_CTX_new();
    if (!evpKey || !mdCtx || EVP_PKEY_set1_EC_KEY(evpKey, sm2Key) != 1)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error creating EVP key";
        goto done;
    }
    evpKeyCtx = EVP_PKEY_CTX_new_from_pkey(NULL, evpKey, NULL);
    if (!evpKeyCtx)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error creating EVP key context";
        goto done;
    }
    EVP_MD_CTX_set_pkey_ctx(mdCtx, evpKeyCtx);
    derSigLen = i2d_ECDSA_SIG(signData, &derSig);
    if (derSigLen <= 0)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error encoding signature";
        goto done;
    }
    if (EVP_DigestVerifyInit(mdCtx, NULL, EVP_sm3(), NULL, evpKey) != 1 ||
        EVP_PKEY_CTX_set1_id(evpKeyCtx, c_userId, sizeof(c_userId) - 1) != 1 ||
        EVP_DigestVerifyUpdate(mdCtx, raw_message_hash->data, raw_message_hash->len) != 1)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_verify: error initializing EVP verification";
        goto done;
    }
    if (EVP_DigestVerifyFinal(mdCtx, derSig, static_cast<size_t>(derSigLen)) == 1)
    {
        ret = WEDPR_SUCCESS;
    }
done:
    if (mdCtx)
    {
        EVP_MD_CTX_free(mdCtx);
    }
    if (evpKeyCtx)
    {
        EVP_PKEY_CTX_free(evpKeyCtx);
    }
    if (evpKey)
    {
        EVP_PKEY_free(evpKey);
    }
    if (derSig)
    {
        OPENSSL_free(derSig);
    }
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    if (point)
    {
        EC_POINT_free(point);
    }
    BN_free(bn);
    if (signData == NULL)
    {
        BN_free(r);
        BN_free(s);
    }
    if (signData)
    {
        ECDSA_SIG_free(signData);
    }
    return ret;
}

// C interface for 'fast_sm2_derive_public_key'.
int8_t bcos::crypto::fast_sm2_derive_public_key(
    const CInputBuffer* raw_private_key, COutputBuffer* output_public_key)
{
    int8_t ret = WEDPR_ERROR;
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    BN_CTX* ctx = NULL;
    char* publicKey = NULL;
    size_t publicKeyLen = 0;
    BIGNUM* privateKey =
        BN_bin2bn((const unsigned char*)raw_private_key->data, raw_private_key->len, NULL);
    if (!privateKey)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error of BN_bin2bn for privateKey";
        goto done;
    }
    // reject invalid private keys (e.g. all-zero), which would otherwise multiply
    // into the point at infinity and produce a 1-byte "public key"
    if (BN_is_zero(privateKey))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error of zero privateKey";
        goto done;
    }
    ctx = BN_CTX_new();
    if (!ctx)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error of BN_CTX_new";
        goto done;
    }
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    if (!sm2Key)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error of EC_KEY_new_by_curve_name";
        goto done;
    }
    if (!EC_KEY_set_private_key(sm2Key, privateKey))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error EC_KEY_set_private_key";
        goto done;
    }
    pubPoint = EC_POINT_new(sm2Group);
    if (!pubPoint)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error EC_POINT_new";
        goto done;
    }
    if (!EC_POINT_mul(sm2Group, pubPoint, privateKey, NULL, NULL, NULL))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_derive_public_key: error of EC_POINT_mul";
        goto done;
    }
    publicKeyLen = EC_POINT_point2buf(sm2Group, pubPoint, POINT_CONVERSION_UNCOMPRESSED,
        reinterpret_cast<unsigned char**>(&publicKey), ctx);
    if (publicKeyLen != c_PUBLICKEY_LEN + 1)
    {
        CRYPTO_LOG(WARNING)
            << "sm2: fast_sm2_derive_public_key: error of EC_POINT_point2bin for publicKey";
        goto done;
    }
    // the destination side of the length contract: COutputBuffer carries a length,
    // so honour it before writing 64 bytes
    if (output_public_key->len < (size_t)c_PUBLICKEY_LEN)
    {
        CRYPTO_LOG(WARNING) << LOG_DESC("sm2: fast_sm2_derive_public_key: output buffer too small")
                            << LOG_KV("len", output_public_key->len);
        goto done;
    }
    // remove the prefix 04
    memcpy(output_public_key->data, publicKey + 1, c_PUBLICKEY_LEN);
    ret = WEDPR_SUCCESS;
done:
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    if (pubPoint)
    {
        EC_POINT_free(pubPoint);
    }
    if (ctx)
    {
        BN_CTX_free(ctx);
    }
    if (privateKey)
    {
        BN_free(privateKey);
    }
    if (publicKey)
    {
        OPENSSL_free(publicKey);
    }
    return ret;
}

// C interface for 'fast_sm2_gen_key_pair'.
int8_t bcos::crypto::fast_sm2_gen_key_pair(
    COutputBuffer* output_public_key, COutputBuffer* output_private_key)
{
    int8_t ret = WEDPR_ERROR;
    // BN_bn2binpad and the derive call below write fixed 32/64-byte outputs; honour the
    // lengths the COutputBuffers carry before touching them
    if (output_public_key->len < (size_t)c_PUBLICKEY_LEN ||
        output_private_key->len < (size_t)c_PRIVATEKEY_LEN)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_gen_key_pair: output buffer too small";
        return ret;
    }
    EC_KEY* sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_gen_key_pair: error of EC_KEY_new_by_curve_name";
        goto done;
    }
    if (!EC_KEY_generate_key(sm2Key))
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_gen_key_pair: error of EC_KEY_generate_key";
        goto done;
    }
    if (BN_bn2binpad(EC_KEY_get0_private_key(sm2Key),
            reinterpret_cast<unsigned char*>(output_private_key->data),
            c_PRIVATEKEY_LEN) != c_PRIVATEKEY_LEN)
    {
        CRYPTO_LOG(WARNING) << "sm2: fast_sm2_gen_key_pair: error of BN_bn2binpad for privateKey";
        goto done;
    }
    {
        CInputBuffer privateKey{output_private_key->data, c_PRIVATEKEY_LEN};
        ret = fast_sm2_derive_public_key(&privateKey, output_public_key);
    }
done:
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    return ret;
}