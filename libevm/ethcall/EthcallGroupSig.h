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

/* @file: EthcallGroupSig.h
 * @author: fisco-dev
 * @date: 2018.02.08
 */

#pragma once
#include <devcore/StatusCode.h>

#include <algorithm/GroupSigFactory.h>
#include <libdevcore/easylog.h>

#include <libevm/ethcall/EthCallEntry.h>

using namespace std;

namespace dev
{
namespace eth
{
//call method
enum CallBackType
{
    CREATE=0x00,
    GENKEY=0x01,
    SIG=0x02,
    VERIFY=0x03,
    OPEN=0x04,
    REVOKE=0x05,
    UPDATEGSK=0x06
};
class EthcallGroupSig: public EthCallExecutor<vector_ref<char>, 
    uint8_t, uint8_t, std::string> 
{
    public:
        EthcallGroupSig()
        { this->registerEthcall(EthCallIdList::GROUP_SIG);}
        
        u256 ethcall(vector_ref<char> result, uint8_t op_type, 
                uint8_t group_method, std::string param)
        {
            LOG(DEBUG)<<"ETHCALL:"<<(int)group_method<<", op_type:"<<(int)op_type;
            int ret;
            switch(op_type)
            {
                case CREATE:
                    ret = create_group(result, GroupSigMethod(group_method), param);
                    break;
                case GENKEY:
                    ret = gen_gsk(result, GroupSigMethod(group_method), param);
                    break;
                case SIG:
                    ret = group_sig(result, GroupSigMethod(group_method), param);
                    break;
                case VERIFY:
                    ret = group_verify(result, GroupSigMethod(group_method), param);
                    break;
                case OPEN:
                    ret = group_open(result, GroupSigMethod(group_method), param);
                    break;
                case REVOKE:
                    ret = gm_revoke(result, GroupSigMethod(group_method), param);
                    break;
                case UPDATEGSK:
                    ret = update_gsk_after_revoke(result, GroupSigMethod(group_method), param);
                default:
                    unkown_op_type(op_type);
                    ret = RetCode::GroupSigStatusCode::OBTAIN_OPTYPE_FAILED;
                    break;
            }
            return (u256)(ret);
        }

        uint64_t gasCost(vector_ref<char> result, uint8_t op_type,
                uint8_t group_method, std::string param)
        { return (result.count() + 2 + param.length());}

        
    private:
        void unkown_op_type(uint8_t& op_type)
        {
            LOG(ERROR)<<"OP_TYPE = "<<op_type<<" UNKOWN OP_TYPE, must be:"
                <<CREATE<<" or "<<GENKEY<<" or "<<SIG<<" or "<<VERIFY<<" or "
                <<OPEN<<" or "<<REVOKE; 
        }
            
        inline void copy_string_to_vector(vector_ref<char>& dst, std::string src)
        {
            char* start_addr = dst.data();
            memcpy(start_addr, src.c_str(), src.length());
        }
        //Group creation: (group public key, group private key)
        int create_group(vector_ref<char>& result, GroupSigMethod method, string& param)
        {
            std::string result_str;
            int ret = (GroupSigFactory::instance(method))->create_group(result_str, param);
            copy_string_to_vector(result, result_str);
            return ret;
        }
           
        //Generate private key for group member
        int gen_gsk(vector_ref<char>& result, GroupSigMethod method, string& param)
        {
            std::string result_str;
            int ret = (GroupSigFactory::instance(method))->gen_gsk(result_str, param);
            copy_string_to_vector(result, result_str);
            return ret;
        }
            
        //Group sig function
        int group_sig(vector_ref<char>& result, GroupSigMethod method, string& param)
        { 
            std::string result_str;
            int ret = (GroupSigFactory::instance(method))->group_sig(result_str, param);
            copy_string_to_vector(result, result_str);
            return ret;
        }

        //Group sig verify
        int group_verify(vector_ref<char>& result, GroupSigMethod method, string& param)
        {
            int valid;
            int ret = (GroupSigFactory::instance(method))->group_verify(valid, param); 
            std::string result_str = "false";
            if(valid == 1)
                result_str = "true";
            copy_string_to_vector(result, result_str);
            LOG(DEBUG)<<"RESULT OF GROUP SIG VERIFY:"<<result_str;
            return ret;
        }
        
        //Group sig open
        int group_open(vector_ref<char>& result, GroupSigMethod method, string& param)
        {
            std::string result_str;
            int ret = (GroupSigFactory::instance(method))->group_open(result_str, param);
            copy_string_to_vector(result, result_str);
            return ret;
        }

        //Group sig revoke
        int gm_revoke(vector_ref<char>& result, GroupSigMethod method, string& param)
        {
            std::string result_str = result.toString();
            int ret = (GroupSigFactory::instance(method))->gm_revoke(result_str, param);
            copy_string_to_vector(result, result_str);
            return ret;
        }
           
            
        //Update other group members' private keys after revoke
        int update_gsk_after_revoke(vector_ref<char>& result, GroupSigMethod method, string& param)
        {
            std::string result_str = result.toString();
            int ret = (GroupSigFactory::instance(method))->update_gsk(result_str, param);
            copy_string_to_vector(result, result_str);
            return ret;
        }

};
}
}
