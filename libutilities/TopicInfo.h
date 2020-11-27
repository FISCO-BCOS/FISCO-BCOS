/**
 *  Copyright (C) 2020 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief: topic item info
 *
 * @file: TopicInfo.h
 * @author: darrenyin
 * @date 2019-08-07
 */
#pragma once
#include <string>
namespace bcos
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
}  // namespace bcos
