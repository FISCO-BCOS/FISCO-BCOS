#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include <iostream>
#include <string>
#include <cstring>

#include "Rsa.h"
using namespace std;

std::string dev::crypto::RSAKeyVerify(const std::string& pubStr, const std::string& strData )
{
    if (pubStr.empty() || strData.empty())
    {
        return "";
    }

    BIO *bio = NULL;
    if ((bio = BIO_new_mem_buf(const_cast<char *>(pubStr.c_str()), -1)) == NULL)
    {
        std::cout << "BIO_new_mem_buf failed!" << std::endl;
        return "";
    }

    std::string strRet;
    RSA* pRSAPublicKey = RSA_new();
    if(PEM_read_bio_RSA_PUBKEY(bio, &pRSAPublicKey, 0, 0) == NULL)
    {
        std::cout << "PEM_read_bio_RSA_PUBKEY failed!" << std::endl;
        return "";
    }

    int nLen = RSA_size(pRSAPublicKey);
    char* pEncode = new char[nLen + 1];
    int ret = RSA_public_decrypt(strData.length(), (const unsigned char*)strData.c_str(), (unsigned char*)pEncode, pRSAPublicKey, RSA_PKCS1_PADDING);
    if (ret >= 0)
    {
        strRet = std::string(pEncode, ret);
    }
    delete[] pEncode;
    RSA_free(pRSAPublicKey);
    CRYPTO_cleanup_all_ex_data();
    return strRet;
}

std::string dev::crypto::RSAKeySign( const std::string& strPemFileName, const std::string& strData )
{
    if (strPemFileName.empty() || strData.empty())
    {
        std::cout << "strPemFileName or strData is empty" << std::endl;
        return "";
    }

    FILE* hPriKeyFile = fopen(strPemFileName.c_str(),"rb");
    if (hPriKeyFile == NULL)
    {
        std::cout << "open file:" << strPemFileName << " faied!" << std::endl;
        return "";
    }

    std::string strRet;
    RSA* pRSAPriKey = RSA_new();
    if(PEM_read_RSAPrivateKey(hPriKeyFile, &pRSAPriKey, 0, 0) == NULL)
    {
        std::cout << "PEM_read_RSAPrivateKey faied!" << std::endl;
        return "";
    }
    int nLen = RSA_size(pRSAPriKey);
    char* pDecode = new char[nLen+1];

    int ret = RSA_private_encrypt(strData.length(), (const unsigned char*)strData.c_str(), (unsigned char*)pDecode, pRSAPriKey, RSA_PKCS1_PADDING);
    if(ret >= 0)
    {
        strRet = std::string((char*)pDecode, ret);
    }
    delete [] pDecode;
    RSA_free(pRSAPriKey);
    fclose(hPriKeyFile);
    CRYPTO_cleanup_all_ex_data();
    return strRet;
}
