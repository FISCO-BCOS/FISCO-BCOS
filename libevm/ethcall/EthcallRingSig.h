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

/* @file: EthcallRingSig.h
 * @author:fisco-dev
 * @date:2018.02.08
 */

#pragma once
#include <algorithm/RingSig.h>
#include <devcore/StatusCode.h>
#include <libdevcore/easylog.h>

#include <libevm/ethcall/EthCallEntry.h>

using namespace std;

namespace dev
{
namespace eth
{
class EthcallRingSig: public EthCallExecutor< vector_ref<char>, 
    std::string, std::string, std::string> 
{
    public:
        EthcallRingSig()
        { this->registerEthcall(EthCallIdList::RING_SIG); }
        

        u256 ethcall(vector_ref<char> result, const std::string sig,
                const std::string message, const std::string param_info)
        {
            bool valid = false;
            LOG(DEBUG)<<"###BEGIN CALLBACK RING SIG VERIFY";
            int internal_ret =  RingSigApi::LinkableRingSig::ring_verify(valid,
                    sig, message, param_info);

            LOG(DEBUG)<<"RESULT of ring verify:"<<valid;
            if(valid)
                copy_string_to_vector(result, "1");
            else
                copy_string_to_vector(result, "0");
            return internal_ret;
        }

        uint64_t gasCost(vector_ref<char> result, const std::string sig,
                const std::string message, const std::string param_info)
        { return result.count() + sig.length() + message.length() + param_info.length();}

     private:      
        inline void copy_string_to_vector(vector_ref<char>& dst, std::string src)
        {
            char* start_addr = dst.data();
            memcpy(start_addr, src.c_str(), src.length());
        }
};
}
}
