/**
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
 * @brief interface for NewViewMsg
 * @file NewViewMsgInterface.h
 * @author: yujiechen
 * @date 2021-04-15
 */
#pragma once
#include "PBFTMessageInterface.h"
#include "ViewChangeMsgInterface.h"
namespace bcos
{
namespace consensus
{
class NewViewMsgInterface : virtual public PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<NewViewMsgInterface>;
    NewViewMsgInterface() = default;
    virtual ~NewViewMsgInterface() {}

    virtual ViewChangeMsgList const& viewChangeMsgList() const = 0;
    virtual void setViewChangeMsgList(ViewChangeMsgList const& _viewChangeMsgs) = 0;

    virtual PBFTMessageList const& prePrepareList() = 0;
    virtual void setPrePrepareList(PBFTMessageList const& _prePrepareList) = 0;
};
}  // namespace consensus
}  // namespace bcos
