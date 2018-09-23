/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file BlockHeader.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */

#include <libdevcore/CommonJS.h>
#include <libdevcore/TrieHash.h>
#include <libdevcrypto/Common.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Transaction.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
namespace dev
{
namespace test
{
class BlockHeaderFixture : public TestOutputHelperFixture
{
public:
    BlockHeaderFixture()
    {
        /// set interface
        block_header_genesis.setParentHash(sha3("parent"));
        block_header_genesis.setRoots(
            sha3("transactionRoot"), sha3("receiptRoot"), sha3("stateRoot"));
        block_header_genesis.setLogBloom(LogBloom(0));
        block_header_genesis.setNumber(int64_t(0));
        block_header_genesis.setGasLimit(u256(3000000));
        block_header_genesis.setGasUsed(u256(100000));
        current_time = utcTime();
        block_header_genesis.setTimestamp(current_time);
        block_header_genesis.appendExtraDataArray(jsToBytes("0x1020"));
        block_header_genesis.setSealer(u256("0x00"));
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(toPublic(Secret::random()));
        }
        block_header_genesis.setSealerList(sealer_list);
        /// construct block
        constructBlock(block_rlp, block_header_genesis);
    }

    /// @brief : construct block
    void constructBlock(RLPStream& block_rlp, BlockHeader const& block_header)
    {
        fakeSingleTransaction();
        block_rlp.appendList(2);
        bytes block_bytes;
        block_header.encode(block_bytes);
        block_rlp.appendRaw(block_bytes);
        RLPStream txs_rlp;
        txs_rlp.appendList(3);
        KeyPair key_pair = KeyPair::create();
        for (int i = 0; i < 3; i++)
        {
            bytes tmp_bytes;
            m_singleTransaction.encode(tmp_bytes);
            txs_rlp.appendRaw(tmp_bytes);
        }
        block_rlp.appendRaw(txs_rlp.out());
    }

    void fakeSingleTransaction()
    {
        bytes rlpBytes = fromHex(
            "f8ac8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a10000000"
            "0000"
            "000000000000003ca576d469d7aa0244071d27eb33c5629753593e00000000000000000000000000000000"
            "0000"
            "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62"
            "657c"
            "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df050948203ea");

        RLP rlpObj(rlpBytes);
        bytesConstRef d = rlpObj.data();
        m_singleTransaction = Transaction(d, eth::CheckTransaction::Everything);
    }

    ~BlockHeaderFixture() { block_header_genesis.clear(); }
    RLPStream block_rlp;
    BlockHeader block_header_genesis;
    Transaction m_singleTransaction;
    uint64_t current_time;
    h512s sealer_list;
};

BOOST_FIXTURE_TEST_SUITE(BlockHeaderTest, BlockHeaderFixture)
/// test getter interfaces of BlockHeader
BOOST_AUTO_TEST_CASE(testBlockerHeaderGetter)
{
    /// get interfaces
    BOOST_CHECK(block_header_genesis.parentHash() == sha3("parent"));
    BOOST_CHECK(block_header_genesis.stateRoot() == sha3("stateRoot"));
    BOOST_CHECK(block_header_genesis.stateRoot() != sha3("stateroot"));
    BOOST_CHECK(block_header_genesis.transactionsRoot() == sha3("transactionRoot"));
    BOOST_CHECK(block_header_genesis.receiptsRoot() == sha3("receiptRoot"));
    BOOST_CHECK(block_header_genesis.logBloom() == LogBloom(0));
    BOOST_CHECK(block_header_genesis.number() == 0);
    BOOST_CHECK(block_header_genesis.gasLimit() == u256(3000000));
    BOOST_CHECK(block_header_genesis.gasUsed() == u256(100000));
    BOOST_CHECK(block_header_genesis.timestamp() == current_time);
    bytes valid_item = block_header_genesis.extraData(0);
    BOOST_CHECK(valid_item.size());
    BOOST_CHECK(valid_item == jsToBytes("0x1020"));
    BOOST_CHECK(block_header_genesis.extraData().size() == 1);
    BOOST_CHECK(block_header_genesis.sealer() == u256("0x00"));
    BOOST_CHECK(block_header_genesis.sealerList() == sealer_list);
}
/// test copy construct && operator == of BlockHeader
BOOST_AUTO_TEST_CASE(testCopyStruct)
{
    /// trans block_header_genesis to RLP
    bytes block_bytes;
    block_header_genesis.encode(block_bytes);
    BlockHeader block_header_copy(block_bytes, HeaderData);
    BOOST_CHECK_THROW(BlockHeader(block_bytes, BlockData), InvalidBlockFormat);
    BOOST_CHECK(block_header_copy.hash() == block_header_genesis.hash());
    ///----- check details: for debug
    BOOST_CHECK(block_header_genesis.parentHash() == block_header_copy.parentHash());
    BOOST_CHECK(block_header_genesis.stateRoot() == block_header_copy.stateRoot());
    BOOST_CHECK(block_header_genesis.transactionsRoot() == block_header_copy.transactionsRoot());
    BOOST_CHECK(block_header_genesis.receiptsRoot() == block_header_copy.receiptsRoot());
    BOOST_CHECK(block_header_genesis.logBloom() == block_header_copy.logBloom());
    BOOST_CHECK(block_header_genesis.number() == block_header_copy.number());
    BOOST_CHECK(block_header_genesis.gasLimit() == block_header_copy.gasLimit());
    BOOST_CHECK(block_header_genesis.gasUsed() == block_header_copy.gasUsed());
    BOOST_CHECK(block_header_genesis.timestamp() == block_header_copy.timestamp());
    BOOST_CHECK(block_header_genesis.extraData() == block_header_copy.extraData());
    BOOST_CHECK(block_header_genesis.sealer() == block_header_copy.sealer());
    BOOST_CHECK(block_header_genesis.sealerList() == block_header_copy.sealerList());
    /// check operator ==
    BOOST_CHECK(block_header_copy == block_header_genesis);
    /// test copy block_header_copy
    BlockHeader block_header_backup(block_header_copy);
    BOOST_CHECK(block_header_copy == block_header_backup);
    BOOST_CHECK(block_header_backup.hash() == block_header_copy.hash());
    /// test assignment
    BlockHeader block_header_copy2;
    block_header_copy2 = block_header_genesis;
    BOOST_CHECK(block_header_copy2 == block_header_genesis);
    BOOST_CHECK(block_header_copy2.hash() == block_header_genesis.hash());
}

/// test pupulateFromParent()
BOOST_AUTO_TEST_CASE(testPopulateFromParent)
{
    BlockHeader block_header_populated;
    block_header_populated.populateFromParent(block_header_genesis);
    BOOST_CHECK(block_header_populated.hash() != block_header_genesis.hash());
    BOOST_CHECK(block_header_populated != block_header_genesis);
    BOOST_CHECK(block_header_populated.stateRoot() == block_header_genesis.stateRoot());
    BOOST_CHECK(block_header_populated.number() == block_header_genesis.number() + 1);
    BOOST_CHECK(block_header_populated.parentHash() == block_header_genesis.hash());
    BOOST_CHECK(block_header_populated.gasLimit() == block_header_genesis.gasLimit());
    BOOST_CHECK(block_header_populated.gasUsed() == u256(0));
}

/// test construct BlockHeader from Block bytes
BOOST_AUTO_TEST_CASE(testConstructFromBlock)
{
    BOOST_REQUIRE_NO_THROW(BlockHeader(block_rlp.out(), BlockData));
    BOOST_CHECK_THROW(BlockHeader(block_rlp.out(), HeaderData), Exception);
    /// test headerHashFromBlock
    BOOST_CHECK(block_header_genesis.headerHashFromBlock(block_rlp.out()));
}

BOOST_AUTO_TEST_CASE(testExtraHeaderException)
{
    /// invalid block format because header invalid
    RLPStream invalid_rlp;
    invalid_rlp << 10;
    BOOST_CHECK_THROW(
        block_header_genesis.extractHeader(ref(invalid_rlp.out())), InvalidBlockFormat);
    // invalid block format because transaction invalid
    RLPStream invalid_txlist;
    invalid_txlist.appendList(2);
    bytes header_bytes;
    block_header_genesis.encode(header_bytes);
    invalid_txlist.appendRaw(header_bytes);
    invalid_txlist.appendRaw(invalid_rlp.out());
    BOOST_CHECK_THROW(
        block_header_genesis.extractHeader(ref(invalid_txlist.out())), InvalidBlockFormat);
}

BOOST_AUTO_TEST_CASE(testBlockHeaderVerify)
{
    /// verify block_header_genesis
    BOOST_REQUIRE_NO_THROW(block_header_genesis.verify(CheckEverything));
    BOOST_REQUIRE_NO_THROW(block_header_genesis.verify(QuickNonce));
    BOOST_REQUIRE_NO_THROW(block_header_genesis.verify(CheckNothingNew));
    /// verify block header and state
    BOOST_CHECK_THROW(block_header_genesis.verify(CheckEverything, ref(block_rlp.out())),
        InvalidTransactionsRoot);
    /// modify state of block_header_genesis, and check, require no throw
    RLP root(block_rlp.out());
    auto txList = root[1];
    auto expectedRoot = trieRootOver(txList.itemCount(), [&](unsigned i) { return rlp(i); },
        [&](unsigned i) { return txList[i].data().toBytes(); });
    block_header_genesis.setRoots(
        expectedRoot, block_header_genesis.receiptsRoot(), block_header_genesis.stateRoot());
    BOOST_REQUIRE_NO_THROW(block_header_genesis.verify(CheckEverything, ref(block_rlp.out())));
    /// populate child and check
    BlockHeader block_header_child;
    block_header_child.populateFromParent(block_header_genesis);
    BOOST_REQUIRE_NO_THROW(block_header_child.verify(CheckEverything, block_header_child));
    /// construct block according to block_header_child
    RLPStream block_child_rlp;
    constructBlock(block_child_rlp, block_header_child);
    root = RLP(block_child_rlp.out());
    txList = root[1];
    expectedRoot = trieRootOver(txList.itemCount(), [&](unsigned i) { return rlp(i); },
        [&](unsigned i) { return txList[i].data().toBytes(); });
    block_header_child.setRoots(
        expectedRoot, block_header_child.receiptsRoot(), block_header_child.stateRoot());
    BOOST_REQUIRE_NO_THROW(block_header_child.verify(
        CheckEverything, block_header_genesis, ref(block_child_rlp.out())));
    /// test invalid timestamp
    block_header_child.setTimestamp(current_time);
    BOOST_CHECK_THROW(block_header_child.verify(
                          CheckEverything, block_header_genesis, ref(block_child_rlp.out())),
        InvalidTimestamp);
    /// test invalid number
    block_header_child.setTimestamp(current_time + 100);
    block_header_child.setNumber(block_header_child.number() + 2);
    BOOST_CHECK_THROW(block_header_child.verify(
                          CheckEverything, block_header_genesis, ref(block_child_rlp.out())),
        InvalidNumber);
    /// test invalid parentHash
    block_header_child.setNumber(block_header_child.number() - 2);
    block_header_child.setParentHash(sha3("+++"));
    BOOST_CHECK_THROW(block_header_child.verify(
                          CheckEverything, block_header_genesis, ref(block_child_rlp.out())),
        InvalidParentHash);
    /// test invalid number
    block_header_child.setNumber(INT64_MAX);
    BOOST_CHECK_THROW(block_header_child.verify(
                          CheckEverything, block_header_genesis, ref(block_child_rlp.out())),
        InvalidNumber);
    // test gas
    block_header_child.setParentHash(block_header_genesis.hash());
    block_header_child.setNumber(block_header_genesis.number() + 1);
    block_header_child.setGasUsed(u256(1000000));
    block_header_child.setGasLimit(u256(1000));
    BOOST_CHECK_THROW(block_header_child.verify(
                          CheckEverything, block_header_genesis, ref(block_child_rlp.out())),
        TooMuchGasUsed);
    BOOST_REQUIRE_NO_THROW(block_header_child.verify(
        CheckNothingNew, block_header_genesis, ref(block_child_rlp.out())));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
