/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: Rsa.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

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
        std::cout << "BIO_new_mem_buf failed!" << "\n";
        return "";
    }

    std::string strRet;
    RSA* pRSAPublicKey = RSA_new();
    if(PEM_read_bio_RSA_PUBKEY(bio, &pRSAPublicKey, 0, 0) == NULL)
    {
        std::cout << "PEM_read_bio_RSA_PUBKEY failed!" << "\n";
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
        std::cout << "strPemFileName or strData is empty" << "\n";
        return "";
    }

    FILE* hPriKeyFile = fopen(strPemFileName.c_str(),"rb");
    if (hPriKeyFile == NULL)
    {
        std::cout << "open file:" << strPemFileName << " faied!" << "\n";
        return "";
    }

    std::string strRet;
    RSA* pRSAPriKey = RSA_new();
    if(PEM_read_RSAPrivateKey(hPriKeyFile, &pRSAPriKey, 0, 0) == NULL)
    {
        std::cout << "PEM_read_RSAPrivateKey faied!" << "\n";
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
