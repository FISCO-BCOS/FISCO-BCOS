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
    VERIFY_SUCCESS_STATUS = 1,  // verify success status,for topic which not need to cert or cert
                                 // operation has been finished  with ok result
    VERIFY_FAILED_STATUS = 2,   // verify failed status, for topic which cert operation has been
                                 // finished with not ok result
};

class TopicItem
{
public:
    std::string topic;
    TopicStatus topicStatus;

public:
    TopicItem() : topicStatus(VERIFYING_STATUS) {}
    bool operator<(const TopicItem& item) const { return this->topic < item.topic; }
};
const std::string topicNeedVerifyPrefix = "#!$TopicNeedVerify_";
const std::string verifyChannelPrefix = "#!$VerifyChannel_";
const std::string pushChannelPrefix = "#!$PushChannel_";
}  // namespace dev
