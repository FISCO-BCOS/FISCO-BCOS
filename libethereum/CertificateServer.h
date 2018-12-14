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
 * @file: CertificateServer.h
 * @author: toxotguo
 * @date: 2018
 */
#pragma once

#include <libdevcore/FixedHash.h>
#include <libethcore/Common.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ChainOperationParams.h>
#include <libdiskencryption/EncryptFile.h>
using namespace std;
using namespace dev;

namespace dev
{
    class CertificateServer
    {
	public :
		CertificateServer();
		void getCertificateList(vector< pair<string,Public> >&  certificates/*[0]ca [1] agency  [2] node */,string &   nodePri);
		void getGmCerttificateList(vector< pair<string,Public> >&  certificates/*[0]ca [1] agency  [2] node [3]enNode*/,string &   nodePri,string&	enNodePri);

		void loadGmFile();
		void loadEcdsaFile();
         
		Signature sign(h256 const & data);
		KeyPair const & keypair()const { return m_keyPair;}

		static CertificateServer& GetInstance();

		EncryptFile getEncryptFile();
	private:
		string m_ca;
		Public m_caPub;
		string m_agency;
		Public m_agencyPub;
		string m_node;
		Public m_nodePub;
		string m_enNode;
		Public m_enNodePub;

		string m_nodePri;
		string m_enNodePri;
        KeyPair m_keyPair;
    };

}
