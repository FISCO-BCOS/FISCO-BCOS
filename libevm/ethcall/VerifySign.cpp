/**
 * @file: VerifySign.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "VerifySign.h"
#include <libdevcore/easylog.h>
#include <libdevcore/CommonData.h>
#include <boost/algorithm/string.hpp>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

u256 VerifySign::ethcall(std::string _hash, std::string _pubs, std::string _signs, std::string _signsIdx)
{
	LOG(DEBUG) << "VerifySign hash:" << _hash <<  ",_pubs:" << _pubs << ",signs:" << _signs << ",_signsIdx:" << _signsIdx;
    try {
        std::vector<std::string> vsign;
        std::vector<std::string> vpub;
        std::vector<std::string> vsignIdx;
        boost::split(vpub, _pubs, boost::is_any_of(";"));
        boost::split(vsign, _signs, boost::is_any_of(";"));
        boost::split(vsignIdx, _signsIdx, boost::is_any_of(";"));
        
        if (vsign.size() != vsignIdx.size())
        {
            LOG(ERROR) << "vsign is eq with vsignIdx size";
            return 1;
        }
        
        if (vsign.size() > vpub.size())
        {
            LOG(ERROR) << "vsign is bigger than pubs size";
            return 1;
        }
        
        
        h256 hash(_hash);
        
        unsigned len = vsign.size();
        for(unsigned i = 0; i < len; i++)
        {
            Signature sig(vsign.at(i));
            unsigned idx = std::stoi(vsignIdx.at(i), nullptr, 0);
            
            Public pub(vpub.at(idx));
            bool ok = dev::verify(pub, sig, hash);
            if (!ok)
            {
                LOG(ERROR) << "VerifySign failed.pub:" << pub.hex() << ",sign:" << sig.hex() << ",hash:" << hash.hex();
                return 1;
            }
        }
        
        return 0;
    } catch(std::exception &e) {
        LOG(ERROR) << "VerifySign error:" << e.what();
    } catch(...) {
        LOG(ERROR) << "VerifySign error.";
    }

    return 1;
}

uint64_t VerifySign::gasCost(std::string _hash, std::string _pubs, std::string _signs, std::string _signsIdx)
{
	return _hash.length() + _pubs.length() + _signs.length() + _signsIdx.length();
}

}
}
