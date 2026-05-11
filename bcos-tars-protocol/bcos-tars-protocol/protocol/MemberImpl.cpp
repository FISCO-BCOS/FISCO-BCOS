/**
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "MemberImpl.h"

bcostars::protocol::MemberImpl::MemberImpl(std::function<bcostars::Member*()> _inner)
  : m_inner(std::move(_inner))
{}

bcostars::protocol::MemberImpl::MemberImpl()
  : m_inner([m_rawMember = bcostars::Member()]() mutable { return &m_rawMember; })
{}

bcostars::protocol::MemberImpl::MemberImpl(std::string const& _memberData) : MemberImpl()
{
    decode(_memberData);
}

bcostars::protocol::MemberImpl::~MemberImpl() = default;

void bcostars::protocol::MemberImpl::setMemberID(std::string const& _memberID)
{
    m_inner()->memberID = _memberID;
}

void bcostars::protocol::MemberImpl::setMemberConfig(std::string const& _config)
{
    m_inner()->memberConfig = _config;
}

std::string const& bcostars::protocol::MemberImpl::memberID() const
{
    return m_inner()->memberID;
}

std::string const& bcostars::protocol::MemberImpl::memberConfig() const
{
    return m_inner()->memberConfig;
}

void bcostars::protocol::MemberImpl::encode(std::string& _encodedData)
{
    tars::TarsOutputStream<tars::BufferWriterString> output;
    m_inner()->writeTo(output);
    output.swap(_encodedData);
}

void bcostars::protocol::MemberImpl::decode(std::string const& _memberData)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer(_memberData.data(), _memberData.length());
    m_inner()->readFrom(input);
}

bcostars::protocol::MemberFactoryImpl::~MemberFactoryImpl() = default;

bcos::protocol::MemberInterface::Ptr bcostars::protocol::MemberFactoryImpl::createMember()
{
    return std::make_shared<MemberImpl>();
}

bcos::protocol::MemberInterface::Ptr bcostars::protocol::MemberFactoryImpl::createMember(
    std::string const& _memberData)
{
    return std::make_shared<MemberImpl>(_memberData);
}
