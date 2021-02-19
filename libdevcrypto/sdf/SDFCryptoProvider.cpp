/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file SDFCryptoProvider.h
 * @author maggiewu
 * @date 2021-02-01
 */
#include "SDFCryptoProvider.h"
#include "csmsds.h"
#include <cstring>
#include <list>
#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <string>
#include <vector>

using namespace std;
namespace dev
{
namespace crypto
{
SessionPool::SessionPool(int size, void* deviceHandle)
{
    m_size = size;
    m_deviceHandle = deviceHandle;
    for (size_t n = 0; n < m_size; n++)
    {
        SGD_HANDLE sessionHandle;
        SGD_RV sessionStatus = SDF_OpenSession(m_deviceHandle, &sessionHandle);
        if (sessionStatus != SDR_OK)
        {
            throw sessionStatus;
        }
        m_pool.push_back(sessionHandle);
    }
}
SessionPool::~SessionPool()
{
    auto iter = m_pool.begin();
    while (iter != m_pool.end())
    {
        SDF_CloseSession(*iter);
        ++iter;
    }
    m_size = 0;
}
void* SessionPool::GetSession()
{
    SGD_HANDLE session = NULL;
    if (m_size == 0)
    {
        SGD_HANDLE sessionHandle;
        SGD_RV sessionStatus = SDF_OpenSession(m_deviceHandle, &sessionHandle);
        if (sessionStatus != SDR_OK)
        {
            throw sessionStatus;
        }
        m_pool.push_back(sessionHandle);
        ++m_size;
    }
    session = m_pool.front();
    m_pool.pop_front();
    --m_size;
    return session;
}

void SessionPool::ReturnSession(void* session)
{
    m_pool.push_back(session);
    ++m_size;
}

SDFCryptoProvider::SDFCryptoProvider()
{
    SGD_RV deviceStatus = SDF_OpenDevice(&m_deviceHandle);
    if (deviceStatus != SDR_OK)
    {
        throw deviceStatus;
    }
    m_sessionPool = new SessionPool(10, m_deviceHandle);
}

SDFCryptoProvider::~SDFCryptoProvider()
{
    delete m_sessionPool;
    if (m_deviceHandle != NULL)
    {
        SDF_CloseDevice(m_deviceHandle);
    }
}

SDFCryptoProvider& SDFCryptoProvider::GetInstance()
{
    static SDFCryptoProvider instance;
    return instance;
}

unsigned int SDFCryptoProvider::Sign(Key const& key, AlgorithmType algorithm,
    unsigned char const* digest, unsigned int const digestLen, unsigned char* signature,
    unsigned int* signatureLen)
{
    switch (algorithm)
    {
    case SM2:
    {
        SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
        SGD_RV signCode;
        if (key.IsInternalKey()){
            SGD_RV getAccessRightCode = SDF_GetPrivateKeyAccessRight(sessionHandle, key.Identifier(), (unsigned char *) key.Password(), (unsigned int)strlen(key.Password()));
            if (getAccessRightCode != SDR_OK){
                m_sessionPool->ReturnSession(sessionHandle);
                return getAccessRightCode;
            }
            signCode = SDF_InternalSign_ECC(sessionHandle, key.Identifier(),(SGD_UCHAR*)digest, digestLen, (ECCSignature*)signature);
            SDF_ReleasePrivateKeyAccessRight(sessionHandle, key.Identifier());
        } else{
            ECCrefPrivateKey eccKey;
            eccKey.bits = 32 * 8;
            memcpy(eccKey.D, key.PrivateKey(), 32);
            signCode = SDF_ExternalSign_ECC(sessionHandle, SGD_SM2_1, &eccKey,
            (SGD_UCHAR*)digest, digestLen, (ECCSignature*)signature);
        }
        if (signCode != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return signCode;
        }
        *signatureLen = 64;
        m_sessionPool->ReturnSession(sessionHandle);
        return SDR_OK;
    }
    default:
        return SDR_ALGNOTSUPPORT;
    }
}

unsigned int SDFCryptoProvider::KeyGen(AlgorithmType algorithm, Key* key)
{
    switch (algorithm)
    {
    case SM2:
    {
        ECCrefPublicKey pk;
        ECCrefPrivateKey sk;
        SGD_UINT32 keyLen = 256;

        SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
        SGD_RV result = SDF_GenerateKeyPair_ECC(sessionHandle, SGD_SM2_3, keyLen, &pk, &sk);
        if (result != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return result;
        }
        unsigned char pk_xy[64];
        memcpy(pk_xy,pk.x,32);
        memcpy(pk_xy+32,pk.y,32);
        key->setPrivateKey(sk.D, 32);
        key->setPublicKey(pk_xy, 64);
        m_sessionPool->ReturnSession(sessionHandle);
        return SDR_OK;
    }
    default:
        return SDR_ALGNOTSUPPORT;
    }
}

unsigned int SDFCryptoProvider::Hash(Key*, AlgorithmType algorithm, unsigned char const* message,
    unsigned int const messageLen, unsigned char* digest, unsigned int* digestLen)
{
    switch (algorithm)
    {
    case SM3:
    {
        SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
        SGD_RV code = SDF_HashInit(sessionHandle, SGD_SM3, NULL, NULL, 0);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }

        code = SDF_HashUpdate(sessionHandle, (SGD_UCHAR*)message, messageLen);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }

        code = SDF_HashFinal(sessionHandle, digest, digestLen);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }
        m_sessionPool->ReturnSession(sessionHandle);
        return code;
    }
    default:
        return SDR_ALGNOTSUPPORT;
    }
}
unsigned int SDFCryptoProvider::HashWithZ(Key*, AlgorithmType algorithm, unsigned char const* zValue,
    unsigned int const zValueLen, unsigned char const* message, unsigned int const messageLen,
    unsigned char* digest, unsigned int* digestLen)
{
    switch (algorithm)
    {
    case SM3:
    {
        SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
        SGD_RV code = SDF_HashInit(sessionHandle, SGD_SM3, NULL, NULL, 0);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }
        code = SDF_HashUpdate(sessionHandle, (SGD_UCHAR*)zValue, zValueLen);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }

        code = SDF_HashUpdate(sessionHandle, (SGD_UCHAR*)message, messageLen);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }

        code = SDF_HashFinal(sessionHandle, digest, digestLen);
        if (code != SDR_OK)
        {
            m_sessionPool->ReturnSession(sessionHandle);
            return code;
        }
        m_sessionPool->ReturnSession(sessionHandle);
        return code;
    }
    default:
        return SDR_ALGNOTSUPPORT;
    }
}

unsigned int SDFCryptoProvider::Verify(Key const& key, AlgorithmType algorithm,
    unsigned char const* digest, unsigned int const digestLen, unsigned char const* signature,
    const unsigned int signatureLen, bool* result)
{
    switch (algorithm)
    {
    case SM2:
    {
        if (signatureLen != 64)
        {
            return SDR_NOTSUPPORT;
        }
        SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
        ECCSignature eccSignature;
        memcpy(eccSignature.r, signature, 32);
        memcpy(eccSignature.s, signature + 32, 32);
        SGD_RV code;

        if (key.IsInternalKey()){
            code = SDF_InternalVerify_ECC(sessionHandle, key.Identifier(), (SGD_UCHAR*)digest, digestLen, &eccSignature);
        } else{
            ECCrefPublicKey eccKey;
            eccKey.bits = 32 * 8;
            memcpy(eccKey.x, key.PublicKey(), 32);
            memcpy(eccKey.y, key.PublicKey() + 32, 32);
            code = SDF_ExternalVerify_ECC(
            sessionHandle, SGD_SM2_1, &eccKey, (SGD_UCHAR*)digest, digestLen, &eccSignature);
        }
        if (code == SDR_OK)
        {
            *result = true;
        }
        else
        {
            *result = false;
        }
        m_sessionPool->ReturnSession(sessionHandle);
        return code;
    }
    default:
        return SDR_ALGNOTSUPPORT;
    }
}

unsigned int SDFCryptoProvider::Encrypt(Key const& key, AlgorithmType algorithm, unsigned char* iv, unsigned char const* plantext,
        unsigned int const plantextLen, unsigned char* cyphertext, unsigned int* cyphertextLen){
     switch (algorithm) {
         case SM4_CBC: {
             SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
             SGD_HANDLE keyHandler;
             SGD_RV importResult = SDF_ImportKey(sessionHandle, (SGD_UCHAR*)key.Symmetrickey(), key.SymmetrickeyLen(), &keyHandler);
             if (!importResult == SDR_OK){
                 m_sessionPool->ReturnSession(sessionHandle);
                return importResult;
             }
             SGD_RV result = SDF_Encrypt(sessionHandle, keyHandler, SGD_SM4_CBC, (SGD_UCHAR*)iv, (SGD_UCHAR*)plantext, plantextLen, (SGD_UCHAR*) cyphertext, cyphertextLen);
             SDF_DestroyKey(sessionHandle, keyHandler);
             m_sessionPool->ReturnSession(sessionHandle);
             return result;
         }
     default:
        return SDR_ALGNOTSUPPORT;
    }
}
unsigned int SDFCryptoProvider::Decrypt(Key const& key, AlgorithmType algorithm, unsigned char* iv, unsigned char const* cyphertext,
        unsigned int const cyphertextLen, unsigned char* plantext, unsigned int* plantextLen){
     switch (algorithm) {
         case SM4_CBC: {
             SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
             SGD_HANDLE keyHandler;
             SGD_RV importResult = SDF_ImportKey(sessionHandle, key.Symmetrickey(), key.SymmetrickeyLen(), &keyHandler);
             if (!importResult == SDR_OK){
                 m_sessionPool->ReturnSession(sessionHandle);
                return importResult;
             }
             SGD_RV result = SDF_Decrypt(sessionHandle, keyHandler, SGD_SM4_CBC, (SGD_UCHAR*)iv, (SGD_UCHAR*)cyphertext, cyphertextLen, (SGD_UCHAR*)plantext, plantextLen);
			 SDF_DestroyKey(sessionHandle, keyHandler);
             m_sessionPool->ReturnSession(sessionHandle);
             return result;
         }
     default:
        return SDR_ALGNOTSUPPORT;
    }
}
unsigned int
SDFCryptoProvider::ExportInternalPublicKey(Key &key, AlgorithmType algorithm) {
  switch (algorithm) {
  case SM2: {
    if (!key.IsInternalKey()) {
      return SDR_ALGNOTSUPPORT;
    }
    ECCrefPublicKey pk;
    SGD_HANDLE sessionHandle = m_sessionPool->GetSession();
    SGD_RV result =
        SDF_ExportSignPublicKey_ECC(sessionHandle, key.Identifier(), &pk);
    if (result != SDR_OK) {
      m_sessionPool->ReturnSession(sessionHandle);
      return result;
    }
    unsigned char pk_xy[64];
    memcpy(pk_xy, pk.x, 32);
    memcpy(pk_xy + 32, pk.y, 32);
    key.setPublicKey(pk_xy, 64);
    m_sessionPool->ReturnSession(sessionHandle);
    return result;
  }
  default:
    return SDR_ALGNOTSUPPORT;
  }
}



char * SDFCryptoProvider::GetErrorMessage(unsigned int code)
{
    switch (code)
    {
    case SDR_OK:
        return (char *)"success";
    case SDR_UNKNOWERR:
        return (char *)"unknown error";
    case SDR_NOTSUPPORT:
        return (char *)"not support";
    case SDR_COMMFAIL:
        return (char *)"communication failed";
    case SDR_OPENDEVICE:
        return (char *)"failed open device";
    case SDR_OPENSESSION:
        return (char *)"failed open session";
    case SDR_PARDENY:
        return (char *)"permission deny";
    case SDR_KEYNOTEXIST:
        return (char *)"key not exit";
    case SDR_ALGNOTSUPPORT:
        return (char *)"algorithm not support";
    case SDR_ALGMODNOTSUPPORT:
        return (char *)"algorithm not support mode";
    case SDR_PKOPERR:
        return (char *)"public key calculate error";
    case SDR_SKOPERR:
        return (char *)"private key calculate error";
    case SDR_SIGNERR:
        return (char *)"signature error";
    case SDR_VERIFYERR:
        return (char *)"verify signature error";
    case SDR_SYMOPERR:
        return (char *)"symmetric crypto calculate error";
    case SDR_STEPERR:
        return (char *)"step error";
    case SDR_FILESIZEERR:
        return (char *)"file size error";
    case SDR_FILENOEXIST:
        return (char *)"file not exist";
    case SDR_FILEOFSERR:
        return (char *)"file offset error";
    case SDR_KEYTYPEERR:
        return (char *)"key type not right";
    case SDR_KEYERR:
        return (char *)"key error";
    default:
        std::string err = "unkown code " + std::to_string(code);
        char * c_err = new char[err.length()+1];
        strcpy(c_err,err.c_str());
        return c_err;
    }
}
}  // namespace crypto
}  // namespace dev
