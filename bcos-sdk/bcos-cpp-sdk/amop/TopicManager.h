/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file TopicManager.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once

#include <boost/thread/thread.hpp>
#include <memory>
#include <mutex>
#include <set>

namespace bcos
{
namespace cppsdk
{
namespace amop
{
// manage the topics user subscribed
class TopicManager
{
public:
    using Ptr = std::shared_ptr<TopicManager>;

    bool addTopic(const std::string& _topic);
    bool addTopics(const std::set<std::string>& _topics);
    bool removeTopic(const std::string& _topic);
    bool removeTopics(const std::set<std::string>& _topics);
    std::set<std::string> topics() const;
    std::string toJson();

private:
    // mutex for m_sessions
    mutable boost::shared_mutex x_topics;
    std::set<std::string> m_topics;
};

}  // namespace amop
}  // namespace cppsdk
}  // namespace bcos