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
/**
 * @file: CertificateServer.cpp
 * @author: toxotguo
 * @date: 2018
 */
#include <libdevcore/FileSystem.h>
#include <libdiskencryption/CryptoParam.h>
#include "CertificateServer.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

EncryptFile CertificateServer::getEncryptFile()
{
	int cryptoMod = 0;
	string superKey = "";
	string kcUrl = "";
	if (dev::getCryptoMod() != 0)
	{
		CryptoParam cryptoParam;
		cryptoParam = cryptoParam.loadDataCryptoConfig(getDataDir()+"/cryptomod.json");
		cryptoMod = cryptoParam.m_cryptoMod;
		superKey = cryptoParam.m_superKey;
		kcUrl = cryptoParam.m_kcUrl;
	}
	EncryptFile _encryptFile = EncryptFile::getInstance(cryptoMod,superKey,kcUrl);
	return _encryptFile;
}

void CertificateServer::loadGmFile()
{
	
	EncryptFile _encryptFile = getEncryptFile();


	auto cacrt = contents(getDataDir() + "/gmca.crt");
	m_ca=asString(cacrt);   
	auto agencycrt = contents(getDataDir() + "/gmagency.crt");
	m_agency = asString(agencycrt);  
	auto nodecrt = contents(getDataDir() + "/gmnode.crt");
	m_node = asString(nodecrt);  

	//auto nodekey = contents(getDataDir() + "/gmnode.key");
	auto nodekey = _encryptFile.getNodeCrtKey();
	m_nodePri = asString(nodekey); 

	auto ennodecrt = contents(getDataDir() + "/gmennode.crt");
	m_enNode = asString(ennodecrt);  

	//auto ennodekey = contents(getDataDir() + "/gmennode.key");
	auto ennodekey = _encryptFile.getEnNodeCrtKey();
	m_enNodePri = asString(ennodekey);

	if( m_ca.empty() || m_agency.empty() || nodecrt.empty() || nodekey.empty() || ennodecrt.empty() || ennodekey.empty())
	{
		LOG(ERROR) << "CertificateServer Init Fail! gmca.crt or gmagency.crt or gmnode.crt or gmnode.key or gmennode.crt or gmennode.key File !";
		exit(-1);
	}

	//auto  nodeprivate = contents(getDataDir() + "/gmnode.private");
	auto  nodeprivate = _encryptFile.getNodePrivateKey();
	string pri = asString(nodeprivate); 
	
	if( pri.size() >= 64 )
	{
		m_keyPair = KeyPair(Secret(fromHex(pri.substr(0,64))) );
		LOG(INFO) << " CertificateServer Load KeyPair " << toPublic(m_keyPair.sec());
	}
	else
	{
		LOG(ERROR) << "CertificateServer Load KeyPair Fail! Please Check gmnode.private File.";
		exit(-1);
	}
}

void CertificateServer::loadEcdsaFile()
{
	EncryptFile _encryptFile = getEncryptFile();

	auto cacrt = contents(getDataDir() + "/ca.crt");
	m_ca=asString(cacrt);   
	auto agencycrt = contents(getDataDir() + "/agency.crt");
	m_agency = asString(agencycrt);  
	auto nodecrt = contents(getDataDir() + "/node.crt");
	m_node = asString(nodecrt);  

	//auto nodekey = contents(getDataDir() + "/node.key");
	auto nodekey = _encryptFile.getNodeCrtKey();
	m_nodePri = asString(nodekey); 

	if( m_ca.empty() || m_agency.empty() || nodecrt.empty() || nodekey.empty() )
	{
		LOG(ERROR) << "CertificateServer Init Fail! ca.crt or agency.crt or node.crt or node.key File !";
		exit(-1);
	}

	//auto  nodeprivate = contents(getDataDir() + "/node.private");
	auto  nodeprivate = _encryptFile.getNodePrivateKey();
	string pri = asString(nodeprivate); 
	if( pri.size() >= 64 )
	{
		m_keyPair = KeyPair(Secret(fromHex(pri.substr(0,64))) );
		LOG(INFO) << " CertificateServer Load KeyPair " << toPublic(m_keyPair.sec());
	}
	else
	{
		LOG(ERROR) << "CertificateServer Load KeyPair Fail! Please Check node.private File.";
		exit(-1);
	}
}
CertificateServer::CertificateServer()
{
#if ETH_ENCRYPTTYPE
	loadGmFile();
#else
	loadEcdsaFile();
#endif
}

void    CertificateServer::getGmCerttificateList(vector< pair<string,Public> >&   certificates/*[0]ca [1] agency  [2] node */,string &   nodepri,string &	enNodePri)
{
	certificates.clear();
	certificates.push_back(make_pair(m_ca,m_caPub));
	certificates.push_back(make_pair(m_agency,m_agencyPub));
	certificates.push_back(make_pair(m_node,m_nodePub));
	certificates.push_back(make_pair(m_enNode,m_enNodePub));

	nodepri=m_nodePri;
	enNodePri = m_enNodePri;
}

void    CertificateServer::getCertificateList(vector< pair<string,Public> >&   certificates/*[0]ca [1] agency  [2] node */,string &   nodepri)
{
	certificates.clear();
	certificates.push_back(make_pair(m_ca,m_caPub));
	certificates.push_back(make_pair(m_agency,m_agencyPub));
	certificates.push_back(make_pair(m_node,m_nodePub));

	nodepri=m_nodePri;
}
Signature    CertificateServer::sign(h256 const & data)
{
	return dev::sign(m_keyPair.sec(), data);
}

CertificateServer& CertificateServer::GetInstance()
{
	static CertificateServer cs;
	return cs;
}