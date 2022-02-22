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
 * @brief interface for ViewChangeMsg
 * @file ViewChangeMsgInterface.h
 * @author: yujiechen
 * @date 2021-04-15
 */
#pragma once
#include "PBFTMessageInterface.h"

namespace bcos
{
namespace consensus
{
class ViewChangeMsgInterface : virtual public PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<ViewChangeMsgInterface>;
    ViewChangeMsgInterface() = default;
    virtual ~ViewChangeMsgInterface() {}

    virtual PBFTProposalInterface::Ptr committedProposal() = 0;
    virtual void setCommittedProposal(PBFTProposalInterface::Ptr _proposal) = 0;

    virtual PBFTMessageList const& preparedProposals() = 0;
    virtual void setPreparedProposals(PBFTMessageList const& _preparedProposal) = 0;
};
using ViewChangeMsgList = std::vector<ViewChangeMsgInterface::Ptr>;
using ViewChangeMsgListPtr = std::shared_ptr<ViewChangeMsgList>;
}  // namespace consensus
}  // namespace bcos
