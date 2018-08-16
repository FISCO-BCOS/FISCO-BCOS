/*
	This file is part of fisco-dev

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with fisco-dev.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Common.h
 * @authors: fisco-dev
 * @date 2018
 */
#pragma once
#include <libweb3jsonrpc/JsonHelper.h>

namespace dev {
    namespace RpcStatusCode {
        const static int VALID_INTEGER = 0;
        //invalid int
        const static int INVALID_INTEGER = 10001;
        //invalid uint
        const static int INVALID_UNSIGNED_INTEGER = 10002;
        //invalid int64_t
        const static int INVALID_INT64 = 10003;
    }
    namespace rpc {
        //construct response
        static void inline constructResponse(Json::Value& response, 
                    const int& ret_code, const std::string& detailInfo)
        {
            response["ret_code"] = ret_code;
            response["detail_info"] = detailInfo;
        }

        static bool inline checkParamInt(int& retValue, const Json::Value& jsonItem, 
                                  Json::Value &response)
        {
            try{
                retValue = jsonItem.asInt();
                return true;

            } catch(std::exception& e) {
                std::string errorMsg = "parse "+ jsonItem.asString() + 
                                      " to int failed, error_msg:" + e.what();
                LOG(ERROR) << errorMsg;
                constructResponse(response, dev::RpcStatusCode::INVALID_INTEGER, errorMsg);
                return false;
            }

        }

        static bool inline checkParamUint(unsigned int& retValue, const Json::Value& jsonItem, 
                                   Json::Value &response)
        {
            try{
                retValue = jsonItem.asUInt();
                return true;
            } catch(std::exception& e) {
                std::string errorMsg = "parse " + jsonItem.asString() + 
                                       "to unsigned int failed, error_msg:" + e.what();
                constructResponse(response, dev::RpcStatusCode::INVALID_UNSIGNED_INTEGER, errorMsg);
                return false;
            }
        }

        static bool inline checkParamInt64( int64_t& retValue, const Json::Value& jsonItem,
                           Json::Value& response)
        {
            try{
                retValue = jsonItem.asInt64();
                return true;
            } catch(std::exception& e) {
                std::string errorMsg = "parse " + jsonItem.asString() + 
                                        "to uint64_t failed, error_msg:" + e.what();
                constructResponse(response, dev::RpcStatusCode::INVALID_INT64, errorMsg);
                return false;
            }
        }

    }
}
