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

#include "AES.h"
#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include <libdevcore/easylog.h>

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

bytes dev::aesCBCEncrypt(bytesConstRef plainData,string const& keyData,int keyLen,bytesConstRef ivData)
{
	string cipherData;
	CryptoPP::AES::Encryption aesEncryption((const byte*)keyData.c_str(), keyLen);
	CryptoPP::CBC_Mode_ExternalCipher::Encryption cbcEncryption( aesEncryption, ivData.data());
	CryptoPP::StreamTransformationFilter stfEncryptor(cbcEncryption, new CryptoPP::StringSink( cipherData ));  
	stfEncryptor.Put( plainData.data(), plainData.size());
	stfEncryptor.MessageEnd();
	//string cipherDataB64;
	/*for( int i = 0; i < cipherData.size(); i++ )
	{  
		char ch[3] = {0};  
		sprintf(ch, "%02x",  static_cast<byte>(cipherData[i]));  
		cipherDataHex += ch;  
	}*/
	//cipherDataB64 = toBase64(bytesConstRef{(const unsigned char*)cipherData.c_str(), cipherData.length()});
	return asBytes(cipherData);
}

bytes dev::aesCBCDecrypt(bytesConstRef cipherData,string const& keyData,int keyLen,bytesConstRef ivData)
{
	string decryptedData;
	CryptoPP::AES::Decryption aesDecryption((const byte*)keyData.c_str(), keyLen); 
	CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption( aesDecryption,ivData.data());  
	CryptoPP::StreamTransformationFilter stfDecryptor(cbcDecryption, new CryptoPP::StringSink( decryptedData ));
	//stfDecryptor.Put( reinterpret_cast<const unsigned char*>( cipherData.c_str() ),cipherLen);
	stfDecryptor.Put(cipherData.data(),cipherData.size());
	stfDecryptor.MessageEnd();
	return asBytes(decryptedData);
}
