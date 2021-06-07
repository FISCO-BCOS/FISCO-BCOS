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
 */
#include "sm2.h"
#include "libdevcore/CommonData.h"
#include <libdevcore/Guards.h>

#define SM3_DIGEST_LENGTH 32

using namespace std;
using namespace dev;

// cache for sign
static dev::Mutex c_zValueCacheMutex;
// map between privateKey to {zValueCache, zValueLen}
static std::map<std::string, std::pair<std::shared_ptr<unsigned char>, size_t>> c_mapTozValueCache;
// max size of c_mapTozValueCache
static const int64_t c_maxMapTozValueCacheSize = 1000;

bool SM2::genKey()
{
    bool lresult = false;
    EC_GROUP* sm2Group = NULL;
    EC_KEY* sm2Key = NULL;
    char* pri = NULL;
    char* pub = NULL;

    sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);
    if (!sm2Group)
    {
        CRYPTO_LOG(ERROR) << "[SM2::genKey] Error Of Gain SM2 Group Object";
        goto err;
    }

    /*Generate SM2 Key*/
    sm2Key = EC_KEY_new();
    if (!sm2Key)
    {
        CRYPTO_LOG(ERROR) << "[SM2::genKey] Error Of Alloc Memory for SM2 Key";
        goto err;
    }

    if (EC_KEY_set_group(sm2Key, (const EC_GROUP*)sm2Group) == 0)
    {
        CRYPTO_LOG(ERROR) << "[SM2::genKey] Error Of Set SM2 Group into key";
        goto err;
    }

    if (EC_KEY_generate_key(sm2Key) == 0)
    {
        CRYPTO_LOG(ERROR) << "[SM2::genKey] Error Of Generate SM2 Key";
        goto err;
    }

    pri = BN_bn2hex(EC_KEY_get0_private_key(sm2Key));
    if (!pri)
    {
        CRYPTO_LOG(ERROR) << "[SM2::genKey] Error Of Output SM2 Private key";
        goto err;
    }
    privateKey = pri;
    // LOG(DEBUG)<<"SM2 PrivateKey:"<<privateKey;

    pub = EC_POINT_point2hex(
        sm2Group, EC_KEY_get0_public_key(sm2Key), POINT_CONVERSION_UNCOMPRESSED, NULL);
    if (!pub)
    {
        CRYPTO_LOG(ERROR) << "[SM2::genKey] Error Of Output SM2 Public key";
        goto err;
    }
    publicKey = pub;
    lresult = true;
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

bool SM2::sign(const char* originalData, int originalDataLen, const string& privateKey,
    unsigned char* r, unsigned char* s)
{
    bool lresult = false;
#ifndef FISCO_SDF
    SM3_CTX sm3Ctx;
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen;
#endif

    EC_KEY* sm2Key = NULL;

    ECDSA_SIG* signData = NULL;
    string str = "";   // big int data
    string _str = "";  // big int swap data
    BIGNUM* res = NULL;
    BN_CTX* ctx = NULL;
    int len = 0;
    int i = 0;

    ctx = BN_CTX_new();

    res = BN_new();
    if (res == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] malloc BigNumber failed";
        goto err;
    }
    BN_hex2bn(&res, (const char*)privateKey.c_str());
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    EC_KEY_set_private_key(sm2Key, res);

#ifdef FISCO_SDF
    const EC_GROUP* group;
    EC_POINT* pubkey;
    group = EC_KEY_get0_group(sm2Key);
    pubkey = EC_POINT_new(group);
    if (!pubkey)
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] get pub point failed";
        goto err;
    }

    if (!EC_POINT_mul(
            EC_KEY_get0_group(sm2Key), pubkey, EC_KEY_get0_private_key(sm2Key), NULL, NULL, NULL))
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] gen pub by d failed";
        goto err;
    }
    EC_KEY_set_public_key(sm2Key, pubkey);
#else
    zValueLen = sizeof(zValue);
    if (!sm2GetZ(privateKey, (const EC_KEY*)sm2Key, zValue, zValueLen))
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] Error Of Compute Z";
        goto err;
    }
    // SM3 Degist
    SM3_Init(&sm3Ctx);
    SM3_Update(&sm3Ctx, zValue, zValueLen);
    SM3_Update(&sm3Ctx, originalData, originalDataLen);
    SM3_Final(zValue, &sm3Ctx);
#endif

#ifdef FISCO_SDF
    signData = SM2_do_sign(sm2Key, EVP_sm3(), (const uint8_t *)SM2_DEFAULT_USERID, strlen(SM2_DEFAULT_USERID), 
        (const uint8_t *)originalData, originalDataLen);
#else
    signData = ECDSA_do_sign_ex(zValue, zValueLen, NULL, NULL, sm2Key);
#endif
    if (signData == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::sign] Error Of SM2 Signature";
        goto err;
    }

#ifdef FISCO_SDF
    len = BN_bn2bin(ECDSA_SIG_get0_r(signData), r);
#else
    len = BN_bn2bin(signData->r, r);
#endif
    for (i = 31; len > 0 && len != 32; --len, --i)
    {
        r[i] = r[len - 1];
    }
    for (; i >= 0 && len != 32; --i)
    {
        r[i] = 0;
    }
#ifdef FISCO_SDF
    len = BN_bn2bin(ECDSA_SIG_get0_s(signData), s);
#else
    len = BN_bn2bin(signData->s, s);
#endif
    for (i = 31; len > 0 && len != 32; --len, --i)
    {
        s[i] = s[len - 1];
    }
    for (; i >= 0 && len != 32; --i)
    {
        s[i] = 0;
    }
    lresult = true;
    // LOG(DEBUG)<<"r:"<<r<<" rLen:"<<r.length()<<" s:"<<s<<" sLen:"<<s.length();
err:
    if (res)
    {
        BN_free(res);
    }
    if (ctx)
        BN_CTX_free(ctx);
    if (sm2Key)
        EC_KEY_free(sm2Key);
    if (signData)
        ECDSA_SIG_free(signData);
    return lresult;
}

int SM2::verify(const unsigned char* _signData, size_t, const unsigned char* _originalData,
    size_t _originalLength, const unsigned char* _publicKey)
{  // _publicKey length must 64, start with 4
    bool lresult = false;
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    EC_GROUP* sm2Group = NULL;
    ECDSA_SIG* signData = NULL;
#ifdef FISCO_SDF
    BIGNUM *bn_r, *bn_s;
#else
    SM3_CTX sm3Ctx;
    unsigned char zValue[SM3_DIGEST_LENGTH];
    size_t zValueLen = SM3_DIGEST_LENGTH;
#endif
    auto pubHex = toHex(_publicKey, _publicKey + 64, "04");
    sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);
    auto rHex = toHex(_signData, _signData + 32, "");
    auto sHex = toHex(_signData + 32, _signData + 64, "");
    if (sm2Group == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_GROUP_new_by_curve_name"
                          << LOG_KV("pubKey", pubHex);
        goto err;
    }

    if ((pubPoint = EC_POINT_new(sm2Group)) == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_POINT_new"
                          << LOG_KV("pubKey", pubHex);
        goto err;
    }

    if (!EC_POINT_hex2point(sm2Group, (const char*)pubHex.c_str(), pubPoint, NULL))
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_POINT_hex2point"
                          << LOG_KV("pubKey", pubHex);
        goto err;
    }

    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);

    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_KEY_new_by_curve_name"
                          << LOG_KV("pubKey", pubHex);
        goto err;
    }

    if (!EC_KEY_set_public_key(sm2Key, pubPoint))
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_KEY_set_public_key"
                          << LOG_KV("pubKey", pubHex);
        goto err;
    }

#ifndef FISCO_SDF
    if (!ECDSA_sm2_get_Z((const EC_KEY*)sm2Key, NULL, NULL, 0, zValue, &zValueLen))
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] Error Of Compute Z" << LOG_KV("pubKey", pubHex);
        goto err;
    }
    // SM3 Degist
    SM3_Init(&sm3Ctx);
    SM3_Update(&sm3Ctx, zValue, zValueLen);
    SM3_Update(&sm3Ctx, _originalData, _originalLength);
    SM3_Final(zValue, &sm3Ctx);
#endif
    /*Now Verify it*/
    signData = ECDSA_SIG_new();
#ifdef FISCO_SDF
    bn_r = BN_bin2bn(_signData, 32, NULL);
    if (!bn_r)
#else
    BN_bin2bn(_signData, 32, signData->r);
    if (!signData->r)
#endif
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of BN_bin2bn r" << LOG_KV("pubKey", pubHex);
        goto err;
    }
    bn_s = BN_bin2bn(_signData + 32, 32, NULL);
    if (!bn_s)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR BN_bin2bn s" << LOG_KV("pubKey", pubHex);
        goto err;
    }
#ifdef FISCO_SDF
    ECDSA_SIG_set0(signData, bn_r, bn_s);
    if (SM2_do_verify(sm2Key, EVP_sm3(), signData, (const uint8_t *)SM2_DEFAULT_USERID, strlen(SM2_DEFAULT_USERID), 
        (const uint8_t *)_originalData, _originalLength) != 1)
#else
    if (ECDSA_do_verify(zValue, zValueLen, signData, sm2Key) != 1)
#endif
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] Error Of SM2 Verify" << LOG_KV("pubKey", pubHex);
        goto err;
    }
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

int SM2::sm2GetZ(std::string const& _privateKey, const EC_KEY* _ecKey, unsigned char* _zValue,
    size_t& _zValueLen)
{
    Guard l(c_zValueCacheMutex);
    // get zValue from the cache
    if (c_mapTozValueCache.count(_privateKey))
    {
        auto cache = c_mapTozValueCache[_privateKey];
        memcpy(_zValue, cache.first.get(), cache.second);
        _zValueLen = cache.second;
        return 1;
    }
#ifdef FISCO_SDF
    auto ret = SM2_compute_z_digest(_zValue, EVP_sm3(), (const uint8_t *)SM2_DEFAULT_USERID, strlen(SM2_DEFAULT_USERID), _ecKey);
#else
    auto ret = ECDSA_sm2_get_Z(_ecKey, NULL, NULL, 0, _zValue, &_zValueLen);
#endif
    // clear the cache if over the capacity limit
    if (c_mapTozValueCache.size() >= c_maxMapTozValueCacheSize)
    {
        c_mapTozValueCache.clear();
    }
    _zValueLen = 32;
    // update the zValue cache
    std::shared_ptr<unsigned char> zValueCache(new unsigned char[SM3_DIGEST_LENGTH]);
    memcpy(zValueCache.get(), _zValue, _zValueLen);
    std::pair<std::shared_ptr<unsigned char>, size_t> cache =
        std::make_pair(zValueCache, _zValueLen);
    c_mapTozValueCache[_privateKey] = cache;
    return ret;
}

int SM2::sm2GetZFromPublicKey(std::string const & _publicKeyHex, unsigned char* _zValue, size_t& _zValueLen){
    bool lresult = false;
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    EC_GROUP* sm2Group = NULL;
    _zValueLen = SM3_DIGEST_LENGTH;
    sm2Group = EC_GROUP_new_by_curve_name(NID_sm2);
    if (sm2Group == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_GROUP_new_by_curve_name"
                          << LOG_KV("pubKey", _publicKeyHex);
        goto err;
    }

    if ((pubPoint = EC_POINT_new(sm2Group)) == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_POINT_new"
                          << LOG_KV("pubKey", _publicKeyHex);
        goto err;
    }

    if (!EC_POINT_hex2point(sm2Group, (const char*)_publicKeyHex.c_str(), pubPoint, NULL))
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_POINT_hex2point"
                          << LOG_KV("pubKey", _publicKeyHex);
        goto err;
    }
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    if (sm2Key == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_KEY_new_by_curve_name"
                          << LOG_KV("pubKey", _publicKeyHex);
        goto err;
    }

    if (!EC_KEY_set_public_key(sm2Key, pubPoint))
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] ERROR of Verify EC_KEY_set_public_key"
                          << LOG_KV("pubKey", _publicKeyHex);
        goto err;
    }
#ifdef FISCO_SDF
    if (!SM2_compute_z_digest(_zValue, EVP_sm3(), (const uint8_t *)SM2_DEFAULT_USERID, strlen(SM2_DEFAULT_USERID), sm2Key))
#else
    if (!ECDSA_sm2_get_Z((const EC_KEY*)sm2Key, NULL, NULL, 0, _zValue, &_zValueLen))
#endif
    {
        CRYPTO_LOG(ERROR) << "[SM2::veify] Error Of Compute Z" << LOG_KV("pubKey", _publicKeyHex);
        goto err;
    }
#ifdef FISCO_SDF
    _zValueLen = 32;
#endif
    lresult = true;
    err:
        if (sm2Key)
            EC_KEY_free(sm2Key);
        if (pubPoint)
            EC_POINT_free(pubPoint);
        if (sm2Group)
            EC_GROUP_free(sm2Group);
        return lresult;
}
#ifdef FISCO_SDF
std::string SM2::get_certpub(const std::string& certfile)
{
    BIO* bio;
    X509* x = NULL;
    EC_KEY* ec;

    BN_CTX* ctx = NULL;
    const EC_POINT* pubPoint = NULL;
    const EC_GROUP* sm2Group = NULL;
    string pubKey = "";
    char* pub = NULL;

    bio = BIO_new_file(certfile.c_str(), "rb");
    if (!bio)
    {
        CRYPTO_LOG(ERROR) << "[SM2::get_certpub] Error Of Alloc Memory for BIO";
        goto err;
    }

    x = d2i_X509_bio(bio, NULL);
    if (!x)
    {
        (void)BIO_reset(bio);
        x = PEM_read_bio_X509_AUX(bio, NULL, NULL, NULL);
        if (!x)
        {
            CRYPTO_LOG(ERROR) << "[SM2::get_certpub] cert not pem or der";
            goto err;
        }
    }

    ec = EVP_PKEY_get0_EC_KEY(X509_get0_pubkey(x));
    if (!ec)
    {
        CRYPTO_LOG(ERROR) << "[SM2::get_certpub] cert pub null";
        goto err;
    }

    ctx = BN_CTX_new();
    if (ctx == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::get_certpub] malloc ctx failed";
        goto err;
    }
    sm2Group = EC_KEY_get0_group(ec);
    pubPoint = EC_KEY_get0_public_key(ec);

    pub = EC_POINT_point2hex(sm2Group, pubPoint, POINT_CONVERSION_UNCOMPRESSED, ctx);
    pubKey = pub;
    pubKey = pubKey.substr(2, 128);

err:
    if (bio)
        BIO_free(bio);
    if (ctx)
        BN_CTX_free(ctx);
    if (x)
        X509_free(x);
    return pubKey;
}
#endif
string SM2::priToPub(const string& pri)
{
    EC_KEY* sm2Key = NULL;
    EC_POINT* pubPoint = NULL;
    const EC_GROUP* sm2Group = NULL;
    string pubKey = "";
    BIGNUM* res = NULL;
    BN_CTX* ctx = NULL;
    char* pub = NULL;
    ctx = BN_CTX_new();
    if (ctx == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::priToPub] malloc ctx failed";
        goto err;
    }
    // LOG(DEBUG)<<"pri:"<<pri;
    res = BN_new();
    if (res == NULL)
    {
        CRYPTO_LOG(ERROR) << "[SM2::priToPub] malloc BigNumber failed";
        goto err;
    }
    BN_hex2bn(&res, (const char*)pri.c_str());
    sm2Key = EC_KEY_new_by_curve_name(NID_sm2);
    if (!EC_KEY_set_private_key(sm2Key, res))
    {
        CRYPTO_LOG(ERROR) << "[SM2::priToPub] Error PriToPub EC_KEY_set_private_key";
        goto err;
    }

    sm2Group = EC_KEY_get0_group(sm2Key);
    pubPoint = EC_POINT_new(sm2Group);

    if (!EC_POINT_mul(sm2Group, pubPoint, res, NULL, NULL, ctx))
    {
        CRYPTO_LOG(ERROR) << "[SM2::priToPub] Error of PriToPub EC_POINT_mul";
        goto err;
    }

    pub = EC_POINT_point2hex(sm2Group, pubPoint, POINT_CONVERSION_UNCOMPRESSED, ctx);
    pubKey = pub;
    pubKey = pubKey.substr(2, 128);
    pubKey = strlower((char*)pubKey.c_str());
    // LOG(DEBUG) << LOG_KV("pri:", pri) << LOG_KV(" pri size:", pri.size())
    //            << LOG_KV("PriToPub:", pubKey) << LOG_KV("PriToPubLen:", pubKey.length());
err:
    if (res)
    {
        BN_free(res);
    }
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