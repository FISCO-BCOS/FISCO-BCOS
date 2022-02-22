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
 * @file EvenPushRequest.h
 * @author: octopus
 * @date 2021-09-01
 */

#pragma once
#include <bcos-rpc/event/EventSubParams.h>

namespace bcos
{
namespace event
{
class EventSubTaskState;
class EventSubUnsubRequest
{
public:
    using Ptr = std::shared_ptr<EventSubUnsubRequest>;

    virtual ~EventSubUnsubRequest() {}

public:
    void setId(const std::string& _id) { m_id = _id; }
    std::string id() const { return m_id; }

    void setGroup(const std::string& _group) { m_group = _group; }
    std::string group() const { return m_group; }

    virtual std::string generateJson() const;
    virtual bool fromJson(const std::string& _request);

private:
    std::string m_id;
    std::string m_group;
};

class EventSubRequest : public EventSubUnsubRequest
{
public:
    using Ptr = std::shared_ptr<EventSubRequest>;

    virtual ~EventSubRequest() {}

public:
    void setParams(std::shared_ptr<EventSubParams> _params) { m_params = _params; }
    std::shared_ptr<EventSubParams> params() const { return m_params; }

    std::string generateJson() const override;
    bool fromJson(const std::string& _request) override;

private:
    std::shared_ptr<EventSubParams> m_params;
    std::shared_ptr<EventSubTaskState> m_state;
};

}  // namespace event
}  // namespace bcos