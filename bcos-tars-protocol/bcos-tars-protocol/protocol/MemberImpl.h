/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief implementation for member
 * @file MemberImpl.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "bcos-tars-protocol/tars/Member.h"
#include <bcos-framework/protocol/MemberInterface.h>
namespace bcostars
{
namespace protocol
{
class MemberImpl : public bcos::protocol::MemberInterface
{
public:
    using Ptr = std::shared_ptr<MemberImpl>;
    MemberImpl(std::function<bcostars::Member*()> _inner);
    MemberImpl();
    MemberImpl(std::string const& _memberData);

    ~MemberImpl() override;

    // the memberID of different service, should be unique
    void setMemberID(std::string const& _memberID) override;
    // the memberConfig of different service
    void setMemberConfig(std::string const& _config) override;

    std::string const& memberID() const override;
    virtual std::string const& memberConfig() const override;

    // encode the member into string
    virtual void encode(std::string& _encodedData) override;

    // decode the member info
    virtual void decode(std::string const& _memberData) override;

private:
    std::function<bcostars::Member*()> m_inner;
};

class MemberFactoryImpl : public bcos::protocol::MemberFactoryInterface
{
public:
    using Ptr = std::shared_ptr<MemberFactoryImpl>;
    MemberFactoryImpl() = default;
    ~MemberFactoryImpl() override;

    bcos::protocol::MemberInterface::Ptr createMember() override;
    bcos::protocol::MemberInterface::Ptr createMember(std::string const& _memberData) override;
};
}  // namespace protocol
}  // namespace bcostars