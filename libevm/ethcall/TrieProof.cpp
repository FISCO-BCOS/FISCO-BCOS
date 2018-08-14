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
 * @file: TrieProof.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <boost/algorithm/string.hpp>
#include <vector>

#include <libdevcore/CommonData.h>
#include <libdevcore/easylog.h>
#include <libdevcore/MemoryDB.h>
#include <libdevcore/TrieDB.h>

#include <libevm/ethcall/TrieProof.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

u256 TrieProof::ethcall(std::string _root, std::string _proofs, std::string _key, std::string _targetValue)
{
	LOG(DEBUG) << "TrieProof root:" << _root <<  ",_proofs:" << _proofs << ",key:" << _key << ",_targetValue:" << _targetValue;
    try {
        std::vector<std::string> proofsTmp;
        std::vector<std::string> proofs;
        boost::split(proofsTmp, _proofs, boost::is_any_of(";"));
        for(std::string s : proofsTmp)
        {
            bytes tmp = fromHex(s);
            std::string p(tmp.begin(), tmp.end());
            proofs.push_back(p);
        }

        unsigned transactionIndex = std::stoi(_key, nullptr, 0);
        RLPStream k;
        k << transactionIndex;
        
        std::pair<bool, std::string> ret = GenericTrieDB<MemoryDB>::verifyProof(_root, k.out(), proofs);
        if (!ret.first)
        {
            LOG(WARNING) << "TrieProof verifyProof failed:" << ", root:" << _root;
            return 1;
        }

        if (sha3(ret.second).hex() == _targetValue)
        {
            return 0;
        }
    } catch(std::exception &e) {
        LOG(WARNING) << "TrieProof error:" << e.what() << ", rooot:" << _root;
    } catch(...) {
        LOG(WARNING) << "TrieProof error." << ", rooot:" << _root;
    }

    return 1;
}

uint64_t TrieProof::gasCost(std::string _root, std::string _proofs, std::string _key, std::string _targetValue)
{
	return _root.length() + _proofs.length() + _key.length() + _targetValue.length();
}

}
}
