/*
	This file is part of FISCO BCOS.

	FISCO BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file: Paillier.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <arpa/inet.h>
#include <string>
#include <vector>

#include <libdevcore/easylog.h>
#include <libdevcore/Common.h>
#include <libpaillier/paillier.h>

#include <libevm/ethcall/Paillier.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

u256 Paillier::ethcall(vector_ref<char> result, std::string d1, std::string d2)
{
    S32 bytelen = 0;
    U8 blen[4];
    CharToByte(d1.c_str(), 4, blen, &bytelen);
    
	U16 len = 0;
	U16 nLen = 0;
	memcpy((char *)&len, (char *)blen, sizeof(len));
	nLen = len;
	len = (ntohs(len) + 2) * 2;

	if (memcmp(d1.c_str(), d1.c_str(), len) != 0)
	{
        return -1;
	}

    std::string str_n = d1.substr(4, len - 4);
	d1 = d1.substr(len);
	d2 = d2.substr(len);
	
	U8 BN_C1[4*2*PaiBNWordLen];
	U8 BN_C2[4*2*PaiBNWordLen];
	U8 BN_N[4*PaiBNWordLen];
	U8 BN_Result[4*2*PaiBNWordLen];
	U8 BN_CryptResult[4*2*PaiBNWordLen + len];

	CharToByte(d1.c_str(), 512, BN_C1, &bytelen);
	CharToByte(d2.c_str(), 512, BN_C2, &bytelen);	
	CharToByte(str_n.c_str(), 256, BN_N, &bytelen);

	PAI_HomAdd(BN_Result, BN_C1, BN_C2, BN_N, PaiBNWordLen);

    memcpy((char *)BN_CryptResult, (char *)&nLen, sizeof(nLen));
    memcpy((char *)(BN_CryptResult + sizeof(nLen)), BN_N, sizeof(BN_N));
    memcpy((char *)(BN_CryptResult + sizeof(nLen) + sizeof(BN_N)), BN_Result, sizeof(BN_Result));

	for (size_t i=0; i<sizeof(BN_CryptResult); ++i)
	{
	    char tmp[3];
		sprintf(tmp, "%02X", BN_CryptResult[i]);
		if(2*i < result.size())
        {
			result[2*i] = tmp[0];
			result[2*i + 1] = tmp[1];
		}
		else
			break;
			//rest += tmp;
	}
   
	LOG(DEBUG) << "paillier d1 " << d1;
	LOG(DEBUG) << "paillier d2 " <<	d2;
	LOG(DEBUG) << "paillier result " <<	result.toString();

    return 0;
}

uint64_t Paillier::gasCost(vector_ref<char> result, std::string d1, std::string d2)
{
	return result.size() + d1.length() + d2.length();
}

}
}
