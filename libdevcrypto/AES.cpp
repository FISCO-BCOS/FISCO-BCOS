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
/** @file AES.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 */
#include <stdlib.h>
#include <string>
#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include <libdevcore/easylog.h>
using namespace std;
#include "AES.h"

#if ETH_ENCRYPTTYPE
#include "sm4/sm4.h"
#endif

using namespace std;
using namespace dev;
using namespace dev::crypto;
using namespace CryptoPP;

bytes dev::aesDecrypt(bytesConstRef _ivCipher, std::string const& _password, unsigned _rounds, bytesConstRef _salt)
{
	bytes pw = asBytes(_password);

	if (!_salt.size())
		_salt = &pw;

	bytes target(64);
	CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA256>().DeriveKey(target.data(), target.size(), 0, pw.data(), pw.size(), _salt.data(), _salt.size(), _rounds);

	try
	{
		CryptoPP::AES::Decryption aesDecryption(target.data(), 16);
		auto cipher = _ivCipher.cropped(16);
		auto iv = _ivCipher.cropped(0, 16);
		CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption(aesDecryption, iv.data());
		std::string decrypted;
		CryptoPP::StreamTransformationFilter stfDecryptor(cbcDecryption, new CryptoPP::StringSink(decrypted));
		stfDecryptor.Put(cipher.data(), cipher.size());
		stfDecryptor.MessageEnd();
		return asBytes(decrypted);
	}
	catch (exception const& e)
	{
		cerr << e.what() << endl;
		return bytes();
	}
}


char* ascii2hex(const char* chs,int len)  
{  
	char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8','9', 'A', 'B', 'C', 'D', 'E', 'F'};

	char *ascii = (char*)calloc ( len * 3 + 1, sizeof(char) );// calloc ascii

	int i = 0;
	while( i < len )
	{
		int b= chs[i] & 0x000000ff;
		ascii[i*2] = hex[b/16] ;
		ascii[i*2+1] = hex[b%16] ;
		++i;
	}
	return ascii;                    
}

#if ETH_ENCRYPTTYPE
bytes dev::gmCBCEncrypt(bytesConstRef plainData,string const& keyData,int keyLen,bytesConstRef ivData)
{
	//LOG(DEBUG)<<"GUOMI SM4 EN TYPE......................";
	//pkcs5模式数据填充
	int padding = plainData.size() % 16;
	int nSize = 16 - padding;
	int inDataVLen = plainData.size() + nSize;
	bytes inDataV(inDataVLen);
	memcpy(inDataV.data(),(unsigned char*)plainData.data(),plainData.size());
	memset(inDataV.data() + plainData.size(),nSize,nSize);

	//数据加密
	bytes enData(inDataVLen);
	SM4::getInstance().setKey((unsigned char*)keyData.data(),keyData.size());
	SM4::getInstance().cbcEncrypt(inDataV.data(), enData.data(), inDataVLen, (unsigned char*)ivData.data(), 1);
	//LOG(DEBUG)<<"ivData:"<<ascii2hex((const char*)ivData.data(),ivData.size());
	return enData;
}

bytes dev::gmCBCDecrypt(bytesConstRef cipherData,string const& keyData,int keyLen,bytesConstRef ivData)
{
	//LOG(DEBUG)<<"GM SM4 DE TYPE....................";
	bytes deData(cipherData.size());
	SM4::getInstance().setKey((unsigned char*)keyData.data(),keyData.size());
	SM4::getInstance().cbcEncrypt((unsigned char*)cipherData.data(), deData.data(), cipherData.size(), (unsigned char*)ivData.data(), 0);
	int padding = deData.data()[cipherData.size() - 1];
	int deLen = cipherData.size() - padding;
	deData.resize(deLen);
	return deData;
}
#endif

bytes dev::origAesCBCEncrypt(bytesConstRef plainData,string const& keyData,int keyLen,bytesConstRef ivData)
{
	//LOG(DEBUG)<<"AES EN TYPE......................";
	string cipherData;
	CryptoPP::AES::Encryption aesEncryption((const byte*)keyData.c_str(), keyLen);
	CryptoPP::CBC_Mode_ExternalCipher::Encryption cbcEncryption( aesEncryption, ivData.data());
	CryptoPP::StreamTransformationFilter stfEncryptor(cbcEncryption, new CryptoPP::StringSink( cipherData ));  
	stfEncryptor.Put( plainData.data(), plainData.size());
	stfEncryptor.MessageEnd();
	return asBytes(cipherData);
}

bytes dev::origAesCBCDecrypt(bytesConstRef cipherData,string const& keyData,int keyLen,bytesConstRef ivData)
{
	//LOG(DEBUG)<<"AES DE TYPE....................";
	string decryptedData;
	CryptoPP::AES::Decryption aesDecryption((const byte*)keyData.c_str(), keyLen); 
	CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption( aesDecryption,ivData.data());  
	CryptoPP::StreamTransformationFilter stfDecryptor(cbcDecryption, new CryptoPP::StringSink( decryptedData ));
	//stfDecryptor.Put( reinterpret_cast<const unsigned char*>( cipherData.c_str() ),cipherLen);
	stfDecryptor.Put(cipherData.data(),cipherData.size());
	stfDecryptor.MessageEnd();
	return asBytes(decryptedData);
}

bytes dev::aesCBCEncrypt(bytesConstRef plainData,string const& keyData,int keyLen,bytesConstRef ivData)
{
#if ETH_ENCRYPTTYPE
	return gmCBCEncrypt(plainData,keyData,keyLen,ivData);
#else
	return origAesCBCEncrypt(plainData,keyData,keyLen,ivData);
#endif
}

bytes dev::aesCBCDecrypt(bytesConstRef cipherData,string const& keyData,int keyLen,bytesConstRef ivData)
{
#if ETH_ENCRYPTTYPE
	return gmCBCDecrypt(cipherData,keyData,keyLen,ivData);
#else
	return origAesCBCDecrypt(cipherData,keyData,keyLen,ivData);
#endif
}