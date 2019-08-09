/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 *
 * @brief: topic item info
 *
 * @file: TopicInfo.h
 * @author: darrenyin
 * @date 2019-08-07
 */
#pragma once
#include <string>
namespace dev
{
enum TopicStatus
{
    VERIFYING_STATUS = 0,  // init status,for topic which  needs to cert before cert operation has
                           // been finished
    VERIFYI_SUCCESS_STATUS = 1,  // verify success status,for topic which not need to cert or cert
                                 // operation has been finished  with ok result
    VERIFYI_FAILED_STATUS = 2,   // verify failed status, for topic which cert operation has been
                                 // finished with not ok result
};

enum CmdForAmop
{
    CMD_AMOP_RPCREUEST = 0x12,           // cmd for rpc request
    CMD_AMOP_HEARBEAT = 0x13,            // cmd for heart beat for sdk
    CMD_AMOP_HANDSHAKE = 0x14,           // cmd for hand shake
    CMD_AMOP_REQUEST = 0x30,             // cmd for request from sdk
    CMD_AMOP_RESPONSE = 0x31,            // cmd for response to sdk
    CMD_AMOP_TOPICREQUEST = 0x32,        // cmd for topic request
    CMD_AMOP_MULBROADCAST = 0x35,        // cmd for mult broadcast
    CMD_REQUEST_RANDVALUE = 0x37,        // cmd request rand value
    CMD_REQUEST_SIGN = 0x38,             // cmd request sign for rand value
    CMD_REQUEST_CHECKSIGN = 0x39,        // cmd request check sign
    CMD_AMOP_PUSH_TRANSACTION = 0x1000,  // cmd for push transaction notify
    CMD_AMOP_PUSH_BLOCKNUM = 0x1001,     // cmd for push  block number 
};

class TopicItem
{
public:
    std::string topic;
    TopicStatus topicStatus;

public:
    TopicItem() : topicStatus(VERIFYING_STATUS) {}
    friend bool operator<(const TopicItem& item1, const TopicItem& item2)
    {
        return item1.topic < item2.topic;
    }
};  // namespace dev
const std::string topicNeedCertPrefix = "needcert_";
}  // namespace dev
