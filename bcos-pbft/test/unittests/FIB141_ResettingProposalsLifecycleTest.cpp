/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief Regression test for FIB-141: m_resettingProposals must NOT be
 *        populated until the tx-hash list has been built and confirmed
 *        non-empty. Empty blocks or exception-throwing blocks must leave
 *        m_resettingProposals untouched, otherwise PBFTConfig::notifySealer()
 *        is permanently blocked.
 * @file FIB141_ResettingProposalsLifecycleTest.cpp
 */
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeTxPool.h"
#include "bcos-pbft/pbft/engine/Validator.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
/// Helper: create a default crypto suite (Keccak256 + Secp256k1).
inline CryptoSuite::Ptr makeCryptoSuite()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    return std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
}

/// Helper: create a TxsValidator with a default txpool/blockfactory.
inline TxsValidator::Ptr makeTxsValidator()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto txPool = std::make_shared<FakeTxPool>();
    auto txResultFactory = std::make_shared<TransactionSubmitResultFactoryImpl>();
    return std::make_shared<TxsValidator>(txPool, blockFactory, txResultFactory);
}

/// Helper: create a Block with no transactions but a valid header.
inline Block::Ptr makeBlockWithNoTxs(BlockNumber _number)
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto block = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(_number);
    header->calculateHash(*cryptoSuite->hashImpl());
    block->setBlockHeader(header);
    return block;
}

/// A Block subclass that throws while accessing transaction hashes — models
/// a malformed block that triggers an exception inside the tx-hash loop in
/// TxsValidator::asyncResetTxsFlag.
class ThrowingBlock : public Block
{
public:
    explicit ThrowingBlock(Block::Ptr _delegate) : m_delegate(std::move(_delegate)) {}

    // The methods exercised by asyncResetTxsFlag — make transactionHash throw.
    uint64_t transactionsHashSize() const override { return 3; }
    HashType transactionHash(uint64_t /*_index*/) const override
    {
        throw std::runtime_error("FIB-141 throwing-block: transactionHash failure");
    }
    BlockHeader::Ptr blockHeader() override { return m_delegate->blockHeader(); }
    AnyBlockHeader blockHeader() const override
    {
        // Forward to the const overload on the delegate.
        return std::as_const(*m_delegate).blockHeader();
    }

    // Other Block methods are not exercised by asyncResetTxsFlag(Block, bool, bool);
    // forward to a real block where possible to keep the impl trivial.
    void decode(bytesConstRef _data, bool _calculateHash, bool _checkSig) override
    {
        m_delegate->decode(_data, _calculateHash, _checkSig);
    }
    void encode(bytes& _encodeData) const override { m_delegate->encode(_encodeData); }
    HashType calculateTransactionRoot(const bcos::crypto::Hash& hashImpl) const override
    {
        return m_delegate->calculateTransactionRoot(hashImpl);
    }
    HashType calculateReceiptRoot(const bcos::crypto::Hash& hashImpl) const override
    {
        return m_delegate->calculateReceiptRoot(hashImpl);
    }
    int32_t version() const override { return m_delegate->version(); }
    void setVersion(int32_t v) override { m_delegate->setVersion(v); }
    BlockType blockType() const override { return m_delegate->blockType(); }
    void setBlockType(BlockType t) override { m_delegate->setBlockType(t); }
    void setBlockHeader(BlockHeader::Ptr h) override { m_delegate->setBlockHeader(h); }
    void setTransaction(uint64_t i, Transaction::Ptr t) override
    {
        m_delegate->setTransaction(i, t);
    }
    void appendTransaction(Transaction::Ptr t) override { m_delegate->appendTransaction(t); }
    void setReceipt(uint64_t i, TransactionReceipt::Ptr r) override
    {
        m_delegate->setReceipt(i, r);
    }
    void appendReceipt(TransactionReceipt::Ptr r) override { m_delegate->appendReceipt(r); }
    void appendTransactionMetaData(TransactionMetaData::Ptr m) override
    {
        m_delegate->appendTransactionMetaData(m);
    }
    uint64_t transactionsSize() const override { return m_delegate->transactionsSize(); }
    uint64_t transactionsMetaDataSize() const override
    {
        return m_delegate->transactionsMetaDataSize();
    }
    uint64_t receiptsSize() const override { return m_delegate->receiptsSize(); }
    void setNonceList(::ranges::any_view<std::string> n) override { m_delegate->setNonceList(n); }
    ::ranges::any_view<std::string> nonceList() const override { return m_delegate->nonceList(); }
    ViewResult<HashType> transactionHashes() const override
    {
        return m_delegate->transactionHashes();
    }
    ViewResult<AnyTransactionMetaData> transactionMetaDatas() const override
    {
        return m_delegate->transactionMetaDatas();
    }
    ViewResult<AnyTransaction> transactions() const override { return m_delegate->transactions(); }
    ViewResult<AnyTransactionReceipt> receipts() const override { return m_delegate->receipts(); }
    size_t size() const override { return m_delegate->size(); }
    bytesConstRef logsBloom() const override { return m_delegate->logsBloom(); }
    void setLogsBloom(bytesConstRef l) override { m_delegate->setLogsBloom(l); }

private:
    Block::Ptr m_delegate;
};

/// Helper: wrap a real (header-only) block into a ThrowingBlock.
inline std::shared_ptr<ThrowingBlock> makeBlockWithThrowingHashAccess(BlockNumber _number)
{
    return std::make_shared<ThrowingBlock>(makeBlockWithNoTxs(_number));
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(FIB141_ResettingProposalsLifecycle, TestPromptFixture)

BOOST_AUTO_TEST_CASE(empty_block_does_not_strand_resetting_hash)
{
    auto validator = makeTxsValidator();
    auto emptyBlock = makeBlockWithNoTxs(/*number*/ 1);

    // Pre-condition: validator starts empty.
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);

    validator->asyncResetTxsFlag(*emptyBlock, true);

    // Post-condition: empty block has nothing to mark, so the proposal hash
    // must NOT be left in m_resettingProposals.
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);
}

BOOST_AUTO_TEST_CASE(exception_during_buildHashList_does_not_strand)
{
    auto validator = makeTxsValidator();
    auto block = makeBlockWithThrowingHashAccess(/*number*/ 2);

    BOOST_CHECK_NO_THROW(validator->asyncResetTxsFlag(*block, true));

    // The exception fires AFTER the (buggy) insert but BEFORE asyncMarkTxs is
    // reached. Without FIB-141 the hash is permanently stranded.
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);
}

BOOST_AUTO_TEST_CASE(empty_block_with_flag_false_is_noop)
{
    // When _flag is false the function should not touch m_resettingProposals
    // at all (this was already correct before FIB-141; the test guards against
    // regression in the reorder).
    auto validator = makeTxsValidator();
    auto emptyBlock = makeBlockWithNoTxs(/*number*/ 3);

    validator->asyncResetTxsFlag(*emptyBlock, false);
    BOOST_CHECK_EQUAL(validator->resettingProposalSize(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
