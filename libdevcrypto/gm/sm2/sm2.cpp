/*
    This file is part of fisco-bcos.

    fisco-bcos is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    fisco-bcos is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with fisco-bcos.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: sm2.cpp
 * @author: websterchen
 *
 * @date: 2018
 * 
 * @author: asherli
 *
 * @date: 2019 
 * use GmSSL
 */
// #pragma once
#include "sm2.h"
#include <libdevcore/easylog.h>
// #include <openssl/ec.h>
// #include <openssl/evp.h>
// #include <openssl/is_gmssl.h>
// #include <openssl/objects.h>
// #include <openssl/sm2.h>
#define SM3_DIGEST_LENGTH 32
using namespace std;

bool SM2::genKey()
{
    bool lresult = false;
    EC_GROUP* sm2Group = NULL;
    EC_KEY* sm2Key = NULL;
    char* pri = NULL;
    char* pub = NULL;

    sm2Group = EC_GROUP_new_by_curve_name(NID_sm2p256v1);
    if (!sm2Group)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::genKey] Error Of Gain SM2 Group Object";
        ERROR_OUTPUT << "[#CRYPTO::SM2::genKey] Error Of Gain SM2 Group Object" << std::endl;
        goto err;
    }

    /*Generate SM2 Key*/
    sm2Key = EC_KEY_new();
    if (!sm2Key)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::genKey] Error Of Alloc Memory for SM2 Key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::genKey] Error Of Alloc Memory for SM2 Key" << std::endl;
        goto err;
    }

    if (EC_KEY_set_group(sm2Key, (const EC_GROUP*)sm2Group) == 0)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::genKey] Error Of Set SM2 Group into key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::genKey] Error Of Set SM2 Group into key" << std::endl;
        goto err;
    }

    if (EC_KEY_generate_key(sm2Key) == 0)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::genKey] Error Of Generate SM2 Key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::genKey] Error Of Generate SM2 Key" << std::endl;
        goto err;
    }

    pri = BN_bn2hex(EC_KEY_get0_private_key(sm2Key));
    if (!pri)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::genKey] Error Of Output SM2 Private key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::genKey] Error Of Output SM2 Private key" << std::endl;
        goto err;
    }
    privateKey = pri;
    // LOG(DEBUG)<<"SM2 PrivateKey:"<<privateKey;

    pub = EC_POINT_point2hex(
        sm2Group, EC_KEY_get0_public_key(sm2Key), POINT_CONVERSION_UNCOMPRESSED, NULL);
    if (!pub)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::genKey] Error Of Output SM2 Public key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::genKey] Error Of Output SM2 Public key" << std::endl;
        goto err;
    }
    publicKey = pub;
    lresult = true;
    // LOG(DEBUG)<<"SM2 PublicKey:"<<publicKey;
err:
    if (pri)
        OPENSSL_free(pri);
    if (pub)
        OPENSSL_free(pub);
    if (sm2Group)
        EC_GROUP_free(sm2Group);
    if (sm2Key)
        EC_KEY_free(sm2Key);
    return lresult;
}

string SM2::getPublicKey()
{
    publicKey = publicKey.substr(2, 128);
    publicKey = strlower((char*)publicKey.c_str());
    return publicKey;
}

string SM2::getPrivateKey()
{
    privateKey = strlower((char*)privateKey.c_str());
    return privateKey;
}

bool SM2::sign(
    const char* originalData, int originalDataLen, const string& privateKey, string& r, string& s)
{
    // originalDataLen:"<<originalDataLen<<" Sign privateKey:"<<privateKey;

    bool lresult = false;
    sm3_ctx_t sm3Ctx;
    EC_KEY* sm2Key = NULL;
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen;
    ECDSA_SIG* signData = NULL;
    char* rData = NULL;
    char* sData = NULL;
    string str = "";   // big int data
    string _str = "";  // big int swap data
    int _size = 0;     // big int padding size
    BIGNUM* res = NULL;
    BN_CTX* ctx = NULL;
    BN_init(res);
    ctx = BN_CTX_new();

    BN_hex2bn(&res, (const char*)privateKey.c_str());
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2p256v1);
    EC_KEY_set_private_key(sm2Key, res);

    zValueLen = sizeof(zValue);
    if (!ECDSA_sm2_get_Z((const EC_KEY*)sm2Key, NULL, NULL, 0, zValue, &zValueLen))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::sign] Error Of Compute Z";
        ERROR_OUTPUT << "[#CRYPTO::SM2::sign] Error Of Compute Z" << std::endl;
        goto err;
    }
    // SM3 Degist
    sm3_init(&sm3Ctx);
    sm3_update(&sm3Ctx, reinterpret_cast<const unsigned char*>(originalData), originalDataLen);
    sm3_final(&sm3Ctx, zValue);

    signData = SM2_do_sign(zValue, zValueLen, sm2Key);
    if (signData == NULL)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::sign] Error Of SM2 Signature";
        ERROR_OUTPUT << "[#CRYPTO::SM2::sign] Error Of SM2 Signature" << std::endl;
        goto err;
    }
    rData = BN_bn2hex(signData->r);
    r = rData;
    sData = BN_bn2hex(signData->s);
    s = sData;

    str = "0000000000000000000000000000000000000000000000000000000000000000";
    if (r.length() < 64)
    {
        _size = 64 - r.length();
        _str = str.substr(0, _size);
        r = _str + r;
    }
    if (s.length() < 64)
    {
        _size = 64 - s.length();
        _str = str.substr(0, _size);
        s = _str + s;
    }
    lresult = true;
    // LOG(DEBUG)<<"r:"<<r<<" rLen:"<<r.length()<<" s:"<<s<<" sLen:"<<s.length();
err:
    if (ctx)
        BN_CTX_free(ctx);
    if (sm2Key)
        EC_KEY_free(sm2Key);
    if (signData)
        ECDSA_SIG_free(signData);
    if (rData)
        OPENSSL_free(rData);
    if (sData)
        OPENSSL_free(sData);
    return lresult;
}

int SM2::verify(const string& _signData, int _signDataLen, const char* originalData,
    int originalDataLen, const string& publicKey)
{
    // LOG(DEBUG)<<"_signData:"<<_signData<<" _signDataLen:"<<_signDataLen<<"
    // originalData:"<<(originalData,originalDataLen)<<"
    // originalDataLen:"<<originalDataLen<<" publicKey:"<<publicKey;
    bool lresult = false;
    sm3_ctx_t sm3Ctx;
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    EC_GROUP* sm2Group = NULL;
    ECDSA_SIG* signData = NULL;
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen = SM3_DIGEST_LENGTH;
    string r = _signData.substr(0, 64);
    string s = _signData.substr(64, 64);
    // LOG(DEBUG)<<"r:"<<r<<" s:"<<s;

    sm2Group = EC_GROUP_new_by_curve_name(NID_sm2p256v1);
    if (sm2Group == NULL)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR of Verify EC_GROUP_new_by_curve_namee";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR of Verify EC_GROUP_new_by_curve_name"
                     << std::endl;
        goto err;
    }

    if ((pubPoint = EC_POINT_new(sm2Group)) == NULL)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR of Verify EC_POINT_new";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR of Verify EC_POINT_new" << std::endl;
        goto err;
    }

    if (!EC_POINT_hex2point(sm2Group, (const char*)publicKey.c_str(), pubPoint, NULL))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR of Verify EC_POINT_hex2point";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR of Verify EC_POINT_hex2point" << std::endl;
        goto err;
    }

    sm2Key = EC_KEY_new_by_curve_name(NID_sm2p256v1);

    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR of Verify EC_KEY_new_by_curve_name";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR of Verify EC_KEY_new_by_curve_name"
                     << std::endl;
        goto err;
    }

    if (!EC_KEY_set_public_key(sm2Key, pubPoint))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR of Verify EC_KEY_set_public_key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR of Verify EC_KEY_set_public_key" << std::endl;
        goto err;
    }

    if (!SM2_compute_message_digest(EVP_sm3(), EVP_sm3(),
            reinterpret_cast<const unsigned char*>(originalData), sizeof(originalData), NULL, NULL,
            zValue, &zValueLen, sm2Key))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] Error Of Compute Z";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] Error Of Compute Z" << std::endl;
        goto err;
    }
    // SM3 Degist
    sm3_init(&sm3Ctx);
    sm3_update(&sm3Ctx, reinterpret_cast<const unsigned char*>(zValue), zValueLen);
    sm3_update(&sm3Ctx, reinterpret_cast<const unsigned char*>(originalData), originalDataLen);
    sm3_final(&sm3Ctx, zValue);

    /*Now Verify it*/
    signData = ECDSA_SIG_new();
    if (!BN_hex2bn(&signData->r, r.c_str()))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR of BN_hex2bn R:" << r;
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR of BN_hex2bn R:" << r << std::endl;
        goto err;
    }

    if (!BN_hex2bn(&signData->s, s.c_str()))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] ERROR BN_hex2bn S:" << s;
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] ERROR BN_hex2bn S:" << s << std::endl;
        goto err;
    }

    if (SM2_do_verify(zValue, zValueLen, signData, sm2Key) != 1)
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::veify] Error Of SM2 Verify";
        ERROR_OUTPUT << "[#CRYPTO::SM2::veify] Error Of SM2 Verify" << std::endl;
        goto err;
    }
    // LOG(DEBUG)<<"SM2 Verify successed.";
    lresult = true;
err:
    if (sm2Key)
        EC_KEY_free(sm2Key);
    if (pubPoint)
        EC_POINT_free(pubPoint);
    if (signData)
        ECDSA_SIG_free(signData);
    if (sm2Group)
        EC_GROUP_free(sm2Group);
    return lresult;
}

string SM2::priToPub(const string& pri)
{
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    const EC_GROUP* sm2Group = NULL;
    string pubKey = "";
    BIGNUM* res;
    BN_CTX* ctx;
    BN_init(res);
    ctx = BN_CTX_new();
    char* pub = NULL;
    // LOG(DEBUG)<<"pri:"<<pri;
    BN_hex2bn(&res, (const char*)pri.c_str());
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2p256v1);
    if (!EC_KEY_set_private_key(sm2Key, res))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::priToPub] Error PriToPub EC_KEY_set_private_key";
        ERROR_OUTPUT << "[#CRYPTO::SM2::priToPub] Error PriToPub EC_KEY_set_private_key"
                     << std::endl;
        goto err;
    }

    sm2Group = EC_KEY_get0_group(sm2Key);
    pubPoint = EC_POINT_new(sm2Group);

    if (!EC_POINT_mul(sm2Group, pubPoint, res, NULL, NULL, ctx))
    {
        CRYPTO_LOG(ERROR) << "[#CRYPTO::SM2::priToPub] Error of PriToPub EC_POINT_mul";
        ERROR_OUTPUT << "[#CRYPTO::SM2::priToPub] Error of PriToPub EC_POINT_mul" << std::endl;
        goto err;
    }

    pub = EC_POINT_point2hex(sm2Group, pubPoint, POINT_CONVERSION_UNCOMPRESSED, ctx);
    pubKey = pub;
    pubKey = pubKey.substr(2, 128);
    pubKey = strlower((char*)pubKey.c_str());
    // LOG(DEBUG)<<"PriToPub:"<<pubKey << "PriToPubLen:" << pubKey.length();
err:
    if (pub)
        OPENSSL_free(pub);
    if (ctx)
        BN_CTX_free(ctx);
    if (sm2Key)
        EC_KEY_free(sm2Key);
    if (pubPoint)
        EC_POINT_free(pubPoint);
    return pubKey;
}

char* SM2::strlower(char* s)
{
    char* str;
    str = s;
    while (*str != '\0')
    {
        if (*str >= 'A' && *str <= 'Z')
        {
            *str += 'a' - 'A';
        }
        str++;
    }
    return s;
}

string SM2::ascii2hex(const char* chs, int len)
{
    char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    char* ascii = (char*)calloc(len * 3 + 1, sizeof(char));  // calloc ascii

    int i = 0;
    while (i < len)
    {
        int b = chs[i] & 0x000000ff;
        ascii[i * 2] = hex[b / 16];
        ascii[i * 2 + 1] = hex[b % 16];
        ++i;
    }
    string str = ascii;
    free(ascii);
    return str;
}

SM2& SM2::getInstance()
{
    static SM2 sm2;
    return sm2;
}