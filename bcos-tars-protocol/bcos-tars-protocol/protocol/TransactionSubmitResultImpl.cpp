/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "TransactionSubmitResultImpl.h"
#include "TransactionReceiptImpl.h"
#include <algorithm>
#include <utility>

bcostars::protocol::TransactionSubmitResultImpl::TransactionSubmitResultImpl()
  : m_inner([inner = bcostars::TransactionSubmitResult()]() mutable { return &inner; })
{}

bcostars::protocol::TransactionSubmitResultImpl::TransactionSubmitResultImpl(
    std::function<bcostars::TransactionSubmitResult*()> inner)
  : m_inner(std::move(inner))
{}

uint32_t bcostars::protocol::TransactionSubmitResultImpl::status() const
{
    return m_inner()->status;
}

void bcostars::protocol::TransactionSubmitResultImpl::setStatus(uint32_t status)
{
    m_inner()->status = static_cast<tars::Int32>(status);
}

bcos::crypto::HashType bcostars::protocol::TransactionSubmitResultImpl::txHash() const
{
    if (m_inner()->txHash.size() == bcos::crypto::HashType::SIZE)
    {
        bcos::crypto::HashType hash;
        std::copy(m_inner()->txHash.begin(), m_inner()->txHash.end(), hash.begin());
        return hash;
    }
    return {};
}

void bcostars::protocol::TransactionSubmitResultImpl::setTxHash(bcos::crypto::HashType txHash)
{
    m_inner()->txHash.assign(txHash.begin(), txHash.end());
}

bcos::crypto::HashType bcostars::protocol::TransactionSubmitResultImpl::blockHash() const
{
    if (m_inner()->blockHash.size() == bcos::crypto::HashType::SIZE)
    {
        bcos::crypto::HashType hash;
        std::copy(m_inner()->blockHash.begin(), m_inner()->blockHash.end(), hash.begin());
        return hash;
    }
    return {};
}

void bcostars::protocol::TransactionSubmitResultImpl::setBlockHash(
    bcos::crypto::HashType blockHash)
{
    m_inner()->blockHash.assign(blockHash.begin(), blockHash.end());
}

int64_t bcostars::protocol::TransactionSubmitResultImpl::transactionIndex() const
{
    return m_inner()->transactionIndex;
}

void bcostars::protocol::TransactionSubmitResultImpl::setTransactionIndex(int64_t index)
{
    m_inner()->transactionIndex = index;
}

bcos::protocol::NonceType bcostars::protocol::TransactionSubmitResultImpl::nonce() const
{
    return {m_inner()->nonce};
}

void bcostars::protocol::TransactionSubmitResultImpl::setNonce(bcos::protocol::NonceType nonce)
{
    m_inner()->nonce = std::move(nonce);
}

bcos::protocol::TransactionReceipt::ConstPtr
bcostars::protocol::TransactionSubmitResultImpl::transactionReceipt() const
{
    return std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
        [innerPtr = &m_inner()->transactionReceipt]() { return innerPtr; });
}

void bcostars::protocol::TransactionSubmitResultImpl::setTransactionReceipt(
    bcos::protocol::TransactionReceipt::ConstPtr transactionReceipt)
{
    auto transactionReceiptImpl =
        std::dynamic_pointer_cast<TransactionReceiptImpl const>(transactionReceipt);
    m_inner()->transactionReceipt = transactionReceiptImpl->inner();
}

bcostars::TransactionSubmitResult const& bcostars::protocol::TransactionSubmitResultImpl::inner()
{
    return *m_inner();
}

std::string const& bcostars::protocol::TransactionSubmitResultImpl::sender() const
{
    return m_inner()->sender;
}

void bcostars::protocol::TransactionSubmitResultImpl::setSender(std::string const& _sender)
{
    m_inner()->sender = _sender;
}

std::string const& bcostars::protocol::TransactionSubmitResultImpl::to() const
{
    return m_inner()->to;
}

void bcostars::protocol::TransactionSubmitResultImpl::setTo(std::string const& _to)
{
    m_inner()->to = _to;
}

uint8_t bcostars::protocol::TransactionSubmitResultImpl::type() const
{
    return m_inner()->type;
}

void bcostars::protocol::TransactionSubmitResultImpl::setType(uint8_t _type)
{
    m_inner()->type = static_cast<decltype(m_inner()->type)>(_type);
}
