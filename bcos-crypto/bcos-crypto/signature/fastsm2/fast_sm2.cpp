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
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sm2.h>

#ifdef WITH_SM2_OPTIMIZE
using namespace bcos;
using namespace bcos::crypto;

const char* c_userId = "1234567812345678";
const int c_R_FIELD_LEN = 32;
const int c_S_FIELD_LEN = 32;
const int c_PUBLICKEY_LEN = 64;
// Note: EC_GROUP_new_by_curve_name cost performance, So only one copy is created globally to
// improve performance
EC_GROUP* sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);

// C interface for 'fast_sm2_sign'.
int8_t bcos::crypto::fast_sm2_sign(const CInputBuffer* raw_private_key,
    const CInputBuffer* raw_public_key, const CInputBuffer* raw_message_hash,
    COutputBuffer* output_signature)
{
    auto hexPubKey =
        toHexString(raw_public_key->data, raw_public_key->data + raw_public_key->len, "04");
    int len = 0;
    // create EC_GROUP
    EC_KEY* sm2Key = NULL;
    ECDSA_SIG* sig = NULL;
    EC_POINT* publicKey = NULL;
    BIGNUM* privateKey = NULL;
    int8_t ret = WEDPR_ERROR;

    // load privateKey
    privateKey = BN_bin2bn((const unsigned char*)raw_private_key->data, raw_private_key->len, NULL);
    if (privateKey == NULL)
    {
        CRYPTO_LOG(ERROR) << LOG_DESC("sm2: fast_sm2_sign: error of BN_bin2bn for privateKey");
        goto done;
    }
    publicKey = EC_POINT_hex2point(sm2Group, hexPubKey->data(), NULL, NULL);
    if (publicKey == NULL)
    {
        CRYPTO_LOG(ERROR) << LOG_DESC("sm2: fast_sm2_sign: error of BN_bin2bn for publicKey");
        goto done;
    }
    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_sign: error of EC_KEY_new";
        goto done;
    }
    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_sign: error of EC_KEY_set_group";
        goto done;
    }
    // set the private key
    if (!EC_KEY_set_private_key(sm2Key, privateKey))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_sign: error of EC_KEY_set_private_key";
        goto done;
    }
    // set the public key
    if (!EC_KEY_set_public_key(sm2Key, publicKey))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_sign: error of EC_KEY_set_public_key";
        goto done;
    }
    sig = sm2_do_sign(sm2Key, EVP_sm3(), (const uint8_t*)c_userId, (size_t)strlen(c_userId),
        (const uint8_t*)raw_message_hash->data, raw_message_hash->len);
    if (sig == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_sign: error of sm2_do_sign";
        goto done;
    }
    // set (r, s) to output_signature
    len = BN_bn2bin(ECDSA_SIG_get0_r(sig), (unsigned char*)output_signature->data);
    if (len < c_R_FIELD_LEN)
    {
        // padding zero to the r field
        memmove(output_signature->data + (c_R_FIELD_LEN - len), output_signature->data, len);
        memset(output_signature->data, 0, (c_R_FIELD_LEN - len));
    }
    // get s filed
    len =
        BN_bn2bin(ECDSA_SIG_get0_s(sig), (unsigned char*)(output_signature->data + c_R_FIELD_LEN));
    if (len < c_S_FIELD_LEN)
    {
        auto startPointer = output_signature->data + c_R_FIELD_LEN;
        // padding zero to the s field
        memmove(startPointer + (c_S_FIELD_LEN - len), startPointer, len);
        memset(startPointer, 0, (c_S_FIELD_LEN - len));
    }
    ret = WEDPR_SUCCESS;
done:
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
    return ret;
}

int8_t bcos::crypto::fast_sm2_verify(const CInputBuffer* raw_public_key,
    const CInputBuffer* raw_message_hash, const CInputBuffer* raw_signature)
{
    auto hexPubKey =
        toHexString(raw_public_key->data, raw_public_key->data + raw_public_key->len, "04");
    EC_KEY* sm2Key = NULL;
    EC_POINT* point = NULL;
    BIGNUM* r = NULL;
    BIGNUM* s = NULL;
    ECDSA_SIG* signData = NULL;
    int8_t ret = WEDPR_ERROR;
    point = EC_POINT_new(sm2Group);
    if (point == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of EC_POINT_new";
        goto done;
    }
    if (!EC_POINT_hex2point(sm2Group, hexPubKey->data(), point, NULL))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of EC_POINT_bin2point";
        goto done;
    }

    sm2Key = EC_KEY_new();
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of EC_KEY_new";
        goto done;
    }

    if (!EC_KEY_set_group(sm2Key, sm2Group))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of EC_KEY_set_group";
        goto done;
    }
    if (!EC_KEY_set_public_key(sm2Key, point))
    {
        CRYPTO_LOG(ERROR) << "EC_KEY_set_public_key of EC_KEY_set_public_key";
        goto done;
    }
    r = BN_bin2bn((const unsigned char*)raw_signature->data, c_R_FIELD_LEN, NULL);
    if (r == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of BN_bin2bn for r";
        goto done;
    }
    s = BN_bin2bn((const unsigned char*)(raw_signature->data + c_R_FIELD_LEN), c_S_FIELD_LEN, NULL);
    if (s == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of BN_bin2bn for s";
        goto done;
    }
    signData = ECDSA_SIG_new();
    if (signData == NULL)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of ECDSA_SIG_new";
        goto done;
    }
    // takes ownership of r and s
    if (!ECDSA_SIG_set0(signData, r, s))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_verify: error of ECDSA_SIG_set0";
        goto done;
    }
    if (sm2_do_verify(sm2Key, EVP_sm3(), signData, (const uint8_t*)c_userId, strlen(c_userId),
            (const uint8_t*)raw_message_hash->data, raw_message_hash->len))
    {
        ret = WEDPR_SUCCESS;
    }
done:
    if (sm2Key)
    {
        EC_KEY_free(sm2Key);
    }
    if (point)
    {
        EC_POINT_free(point);
    }
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
    BIGNUM* privateKey =
        BN_bin2bn((const unsigned char*)raw_private_key->data, raw_private_key->len, NULL);
    if (!privateKey)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_derive_public_key: error of BN_bin2bn for privateKey";
        goto done;
    }
    ctx = BN_CTX_new();
    if (!ctx)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_derive_public_key: error of BN_CTX_new";
        goto done;
    }
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    if (!sm2Key)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_derive_public_key: error of EC_KEY_new_by_curve_name";
        goto done;
    }
    if (!EC_KEY_set_private_key(sm2Key, privateKey))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_derive_public_key: error EC_KEY_set_private_key";
        goto done;
    }
    pubPoint = EC_POINT_new(sm2Group);
    if (!pubPoint)
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_derive_public_key: error EC_POINT_new";
        goto done;
    }
    if (!EC_POINT_mul(sm2Group, pubPoint, privateKey, NULL, NULL, NULL))
    {
        CRYPTO_LOG(ERROR) << "sm2: fast_sm2_derive_public_key: error of EC_POINT_mul";
        goto done;
    }
    if (!EC_POINT_point2buf(
            sm2Group, pubPoint, POINT_CONVERSION_UNCOMPRESSED, (unsigned char**)&publicKey, ctx))
    {
        CRYPTO_LOG(ERROR)
            << "sm2: fast_sm2_derive_public_key: error of EC_POINT_point2bin for publicKey";
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

#endif