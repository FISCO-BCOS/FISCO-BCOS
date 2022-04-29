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
 * @brief the member information
 * @file MemberInterface.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include <memory>
namespace bcos
{
namespace protocol
{
class MemberInterface
{
public:
    using Ptr = std::shared_ptr<MemberInterface>;
    MemberInterface() = default;
    virtual ~MemberInterface() {}

    // the memberID of different service, should be unique
    virtual void setMemberID(std::string const& _memberID) = 0;
    // the memberConfig of different service
    virtual void setMemberConfig(std::string const& _config) = 0;
    virtual void setSeq(int64_t _seq) { m_seq = _seq; }

    virtual std::string const& memberID() const = 0;
    virtual std::string const& memberConfig() const = 0;
    virtual int64_t seq() const { return m_seq; }

    // encode the member into string
    virtual void encode(std::string& _encodedData) = 0;
    // decode the member info
    virtual void decode(std::string const& _memberData) = 0;

protected:
    int64_t m_seq;
};

class MemberFactoryInterface
{
public:
    using Ptr = std::shared_ptr<MemberFactoryInterface>;
    MemberFactoryInterface() = default;
    virtual ~MemberFactoryInterface() {}

    virtual MemberInterface::Ptr createMember() = 0;
    virtual MemberInterface::Ptr createMember(std::string const& _memberData) = 0;
};
}  // namespace protocol
}  // namespace bcos