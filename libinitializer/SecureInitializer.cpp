/*
 * SecureInitializer.cpp
 *
 *  Created on: 2018年5月10日
 *      Author: mo nan
 */

#include "SecureInitializer.h"
#include <libdevcore/CommonIO.h>
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <iostream>
#include <openssl/rsa.h>
#include <openssl/engine.h>
#include <libdevcore/easylog.h>
#include <boost/algorithm/string/replace.hpp>

using namespace dev;
using namespace dev::eth;

void SecureInitializer::initConfig(const boost::property_tree::ptree &pt) {
	std::string key = pt.get<std::string>("secure.key", "${DATAPATH}/node.key");
	std::string cert = pt.get<std::string>("secure.cert", "${DATAPATH}/node.crt");
	std::string caCert = pt.get<std::string>("secure.ca_cert", "${DATAPATH}/ca.crt");
	std::string caPath = pt.get<std::string>("secure.ca_path", "");

	completePath(key);
	completePath(cert);
	completePath(caCert);
	completePath(caPath);

	bytes keyContent;
	if(!key.empty()) {
		//指定了key目录
		try {
			keyContent = contents(key);
		}
		catch(std::exception &e) {
			LOG(ERROR) << "Open PrivateKey file: " << key << " failed";

			BOOST_THROW_EXCEPTION(PrivateKeyError());
		}
	}

	std::shared_ptr<EC_KEY> ecKey;
	if(!keyContent.empty()) {
		try {
			LOG(DEBUG) << "Load existing PrivateKey";
			std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [&](BIO *p) {
				BIO_free(p);
			});
			BIO_write(bioMem.get(), keyContent.data(), keyContent.size());

			std::shared_ptr<EVP_PKEY> evpPKey(PEM_read_bio_PrivateKey(bioMem.get(), NULL, NULL,NULL), [](EVP_PKEY *p) {
				EVP_PKEY_free(p);
			});

			if(!evpPKey) {
				BOOST_THROW_EXCEPTION(PrivateKeyError());
			}

			ecKey.reset(EVP_PKEY_get1_EC_KEY(evpPKey.get()), [](EC_KEY *p) {
				EC_KEY_free(p);
			});
		}
		catch(dev::Exception &e) {
			LOG(ERROR) << "Parse PrivateKey failed: " << e.what();

			BOOST_THROW_EXCEPTION(e);
		}
	}
	else {
		LOG(ERROR) << "Privatekey not exists!";

		BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
#if 0
		LOG(DEBUG) << "PrivateKey not exists, generate";

		ecKey.reset(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), [] (EC_KEY *p) {
			EC_KEY_free(p);
		});

		EC_KEY_generate_key(ecKey.get());

		if(!ecKey) {
			LOG(ERROR) << "Generate PrivateKey failed";

			BOOST_THROW_EXCEPTION(PrivateKeyError());
		}

		std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [&](BIO *p) {
			BIO_free(p);
		});

		if(!PEM_write_bio_ECPrivateKey(bioMem.get(), ecKey.get(), NULL, NULL, 0, NULL, NULL)) {
			LOG(ERROR) << "Write PrivateKey to bio failed";

			BOOST_THROW_EXCEPTION(PrivateKeyError());
		}

		std::shared_ptr<char> privateKeyBuffer(new char[1024 * 64], [](const char *p) {
			delete[] p;
		});
		int length = BIO_read(bioMem.get(), privateKeyBuffer.get(), 1024 * 64);

		keyContent.assign(privateKeyBuffer.get(), privateKeyBuffer.get() + length);

		dev::writeFile(key, keyContent, false);
#endif
	}

	std::shared_ptr<const BIGNUM> ecPrivateKey(EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM *p) {
	});

	std::shared_ptr<char> privateKeyData(BN_bn2hex(ecPrivateKey.get()), [](char *p) {
		OPENSSL_free(p);
	});

	std::string keyHex(privateKeyData.get()); //取出key数据

	_key = KeyPair(Secret(keyHex));

	try {
		_sslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

		std::shared_ptr<EC_KEY> ecdh(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), [](EC_KEY *p) {
			EC_KEY_free(p);
		});
		SSL_CTX_set_tmp_ecdh(_sslContext->native_handle(), ecdh.get());

		_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
		LOG(DEBUG) << "NodeID:" << _key.pub().hex();

		boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
		_sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

		if(!cert.empty() && !contents(cert).empty()) {
			LOG(DEBUG) << "Use user certificate file: " << cert;

			_sslContext->use_certificate_chain_file(cert);
			_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
		}
		else{
			LOG(ERROR) << "Certificate not exists!";

			BOOST_THROW_EXCEPTION(CertificateNotExists());
#if 0
			LOG(DEBUG) << "Generate temporary certificate";

			std::shared_ptr<EVP_PKEY> evpKey(EVP_PKEY_new(), [](EVP_PKEY *p) {
				EVP_PKEY_free(p);
			});

			EVP_PKEY_set1_EC_KEY(evpKey.get(), ecKey.get());

			//生成一个临时的X509 cert, 握手时提供公钥信息
			std::shared_ptr<X509> x509(X509_new(), [](X509 *p) {
				X509_free(p);
			});
			X509_set_pubkey(x509.get(), evpKey.get());
			X509_sign(x509.get(), evpKey.get(), EVP_sha256());

			//SSL_CTX_set_cipher_list(_sslContext->native_handle(), "ALL");
			SSL_CTX_set_default_verify_paths(_sslContext->native_handle());

			SSL_CTX_use_certificate(_sslContext->native_handle(), x509.get());
			SSL_CTX_add_extra_chain_cert(_sslContext->native_handle(), x509.get());
#endif
			//_sslContext->use_tmp_dh(dh)
		}

		auto caCertContent = contents(caCert);
		if(!caCert.empty() && !caCertContent.empty()) {
			LOG(DEBUG) << "Use ca certificate file: " << caCert;

			_sslContext->add_certificate_authority(boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
			_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
		}

		if(!caPath.empty()) {
			LOG(DEBUG) << "Use ca path: " << caPath;

			_sslContext->add_verify_path(caPath);
			_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
		}
	}
	catch(Exception &e) {
		LOG(ERROR) << "load verify file failed: ";

		BOOST_THROW_EXCEPTION(e);
	}
}

KeyPair& SecureInitializer::key() {
	return _key;
}

std::shared_ptr<boost::asio::ssl::context> SecureInitializer::sslContext() {
	return _sslContext;
}

void SecureInitializer::setDataPath(std::string dataPath) {
	_dataPath = dataPath;
}

void SecureInitializer::completePath(std::string &path) {
	boost::algorithm::replace_first(path, "${DATAPATH}", _dataPath + "/");
}
