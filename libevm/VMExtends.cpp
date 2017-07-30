/** @file VMExtends.cpp
 * @author ximi
 * @date 2017.05.24
 */

#include <arpa/inet.h>
#include "VMExtends.h"
#include <boost/algorithm/string.hpp>
#include "pailler.h"

using namespace std;
using namespace dev;
using namespace dev::eth;

ExtendRegistrar* ExtendRegistrar::s_this = nullptr;

ExtendExecutor const& ExtendRegistrar::executor(std::string const& _name)
{
	if (!get()->m_execs.count(_name))
		BOOST_THROW_EXCEPTION(ExtendExecutorNotFound());
	return get()->m_execs[_name];
}

std::string ExtendRegistrar::parseExtend(std::string const& _param, int & _paramOff)
{
	std::string ret;
	_paramOff = 0;
	const std::string extendTag("[69d98d6a04c41b4605aacb7bd2f74bee]");
	const int tagLen = 34;//[69d98d6a04c41b4605aacb7bd2f74bee]
	const int minLen = 39;//[69d98d6a04c41b4605aacb7bd2f74bee][01a]

	if (_param.length() < minLen
		|| _param.substr(0, tagLen) != extendTag)
		return ret;

	int cmdLen = ::atoi(_param.substr(tagLen+1, 2).c_str());
	if (cmdLen <= 0)
	{
		return ret;
	}
	std::string cmd(_param.c_str()+tagLen+3, cmdLen);
	if (cmd.length() != (unsigned)cmdLen)
	{
		return ret;
	}

	_paramOff = tagLen+cmdLen+4;
	ret = cmd;
	std::transform(ret.begin(), ret.end(), ret.begin(), ::tolower);

	return ret;
}

namespace
{
static int paillier(std::string& d1, std::string& d2, std::string& result)
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
        result += tmp;
	}

	return 0;
}

static void SplitString(const string &pm_sExpression, char pm_cDelimiter, vector<string> &pm_vecArray)
{
    size_t iPosBegin = 0;
    size_t iPosEnd;
    while ((iPosEnd = pm_sExpression.find(pm_cDelimiter, iPosBegin)) != string::npos)
    {
        if (iPosEnd>iPosBegin)
            pm_vecArray.push_back(pm_sExpression.substr(iPosBegin, iPosEnd - iPosBegin));
        iPosBegin = iPosEnd + 1;
    }
    
    pm_vecArray.push_back(pm_sExpression.substr(iPosBegin));
}

ETH_REGISTER_EXTEND(paillier)(std::string const& _param, char* _ptmem)
{
	uint32_t ret = 1;
	std::vector<std::string> vargs;
	SplitString(_param, '|', vargs);

	if (vargs.size() < 3 || vargs[0].length() != vargs[1].length())
	{
		return ret;
	}
	else
	{
		std::string result;
		if (paillier(vargs[0], vargs[1], result) == 0)
		{
			::memcpy(_ptmem + atoi(vargs[2].c_str()), result.c_str(), result.length());
			ret = 0;
		}
	}

	return ret;
}
}
