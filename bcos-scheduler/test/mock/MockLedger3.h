#pragma once

#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/BlockImpl.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <gsl/span>
#include <map>
#include <range/v3/algorithm/count.hpp>

using namespace bcos::ledger;


namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockLedger3 : public bcos::ledger::LedgerInterface
{
public:
    MockLedger3() : LedgerInterface() {}
    using Ptr = std::shared_ptr<MockLedger3>;

    task::Task<std::string> prewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        bool writeTxsAndReceipts, std::optional<bcos::crypto::HashType> blockHashOverride,
        bool writeNonces) override
    {
        auto blockNumber = block->blockHeader()->number();
        if (blockNumber == 1024)
        {
            BOOST_THROW_EXCEPTION(
                BCOS_ERROR(LedgerError::CollectAsyncCallbackError, "PrewriteBlock error"));
        }
        co_return std::string{};
    }

    task::Task<void> preStoreBlockTxs(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override
    {
        co_return;
    }

    bcos::Error::Ptr storeTransactionsAndReceipts(bcos::protocol::ConstTransactionsPtr blockTxs,
        bcos::protocol::Block::ConstPtr block) override
    {
        return nullptr;
    }

    task::Task<protocol::Block::Ptr> getBlockDataByNumber(
        protocol::BlockNumber _blockNumber, int32_t _blockFlag) override
    {
        auto block = std::make_shared<bcostars::protocol::BlockImpl>();
        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
        blockHeader->setNumber(_blockNumber);
        block->setBlockHeader(blockHeader);
        if (_blockFlag & HEADER)
        {
            co_return nullptr;
        }
        co_return std::move(block);
    }

    task::Task<protocol::BlockNumber> getBlockNumber() override { co_return commitBlockNumber; }

    task::Task<crypto::HashType> getBlockHashByNumber(protocol::BlockNumber _blockNumber) override
    {
        BOOST_CHECK_EQUAL(_blockNumber, commitBlockNumber);
        co_return h256(commitBlockNumber);
    }

    task::Task<protocol::BlockNumber> getBlockNumberByHash(crypto::HashType) override
    {
        co_return -1;
    }

    task::Task<BatchTransactionsResult> getBatchTxsByHashList(crypto::HashListPtr, bool) override
    {
        co_return BatchTransactionsResult{};
    }

    task::Task<TransactionReceiptResult> getTransactionReceiptByHash(
        crypto::HashType, bool) override
    {
        co_return TransactionReceiptResult{};
    }

    task::Task<TransactionCountResult> getTotalTransactionCount() override
    {
        co_return TransactionCountResult{};
    }

    task::Task<std::optional<bcos::storage::Entry>> getCurrentStateByKey(std::string_view) override
    {
        co_return std::nullopt;
    }

    task::Task<SystemConfigEntry> getSystemConfigByKey(std::string_view _key) override
    {
        if (_key == ledger::SYSTEM_KEY_TX_COUNT_LIMIT)
        {
            co_return SystemConfigEntry{"100", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD)
        {
            co_return SystemConfigEntry{"300", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_TX_GAS_LIMIT)
        {
            co_return SystemConfigEntry{"300000000", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_TX_GAS_PRICE)
        {
            co_return SystemConfigEntry{"0x1", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_COMPATIBILITY_VERSION)
        {
            co_return SystemConfigEntry{bcos::protocol::RC4_VERSION_STR, commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_RPBFT_SWITCH)
        {
            co_return SystemConfigEntry{"1", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM)
        {
            co_return SystemConfigEntry{"4", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM)
        {
            co_return SystemConfigEntry{"1000", commitBlockNumber};
        }
        if (_key == ledger::INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE)
        {
            co_return SystemConfigEntry{"0", commitBlockNumber};
        }
        if (_key == ledger::SYSTEM_KEY_WEB3_CHAIN_ID)
        {
            co_return SystemConfigEntry{"20200", commitBlockNumber};
        }
        if (::ranges::count(ledger::Features::featureKeys(), _key) > 0)
        {
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Not found!"));
        }
        if (_key == SYSTEM_KEY_AUTH_CHECK_STATUS)
        {
            co_return SystemConfigEntry{"0", commitBlockNumber};
        }
        BOOST_FAIL("Unknown query key: " + std::string(_key));
        co_return SystemConfigEntry{"", 0};
    }

    task::Task<bcos::ledger::SystemConfigs> fetchAllSystemConfigs(
        protocol::BlockNumber _blockNumber) override
    {
        ledger::SystemConfigs systemConfig;
        systemConfig.set(ledger::SYSTEM_KEY_TX_COUNT_LIMIT, "100", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, "300", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_TX_GAS_LIMIT, "300000000", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_TX_GAS_PRICE, "0x1", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_COMPATIBILITY_VERSION, bcos::protocol::RC4_VERSION_STR,
            commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_RPBFT_SWITCH, "1", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, "4", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, "1000", commitBlockNumber);
        systemConfig.set(ledger::SYSTEM_KEY_WEB3_CHAIN_ID, "20200", commitBlockNumber);
        co_return systemConfig;
    }

    task::Task<consensus::ConsensusNodeList> getNodeListByType(std::string_view _type) override
    {
        if (_type == ledger::CONSENSUS_SEALER)
        {
            co_return consensus::ConsensusNodeList(1);
        }
        if (_type == ledger::CONSENSUS_OBSERVER)
        {
            co_return consensus::ConsensusNodeList(2);
        }
        if (_type == ledger::CONSENSUS_CANDIDATE_SEALER)
        {
            co_return consensus::ConsensusNodeList(2);
        }
        if (_type.empty())
        {
            consensus::ConsensusNodeList nodeList(5);
            nodeList[0].type = consensus::Type::consensus_sealer;
            nodeList[1].type = consensus::Type::consensus_observer;
            nodeList[2].type = consensus::Type::consensus_observer;
            nodeList[3].type = consensus::Type::consensus_candidate_sealer;
            nodeList[4].type = consensus::Type::consensus_candidate_sealer;
            co_return std::move(nodeList);
        }
        BOOST_FAIL("Unknown query type");
        co_return consensus::ConsensusNodeList{};
    }

    task::Task<NonceListResult> getNonceList(protocol::BlockNumber, int64_t) override
    {
        co_return nullptr;
    }

    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) override {}

    void commitSuccess(bool _success)
    {
        if (_success)
        {
            ++commitBlockNumber;
            SCHEDULER_LOG(DEBUG) << "---- mockLedger -----"
                                 << LOG_KV("CommitBlock success, commitBlockNumber",
                                        commitBlockNumber);
        }
        else
        {
            SCHEDULER_LOG(DEBUG) << "---- mockLedger -----"
                                 << LOG_KV(
                                        "CommitBlock failed, commitBlockNumber", commitBlockNumber);
        }
    }

private:
    bcos::protocol::BlockNumber commitBlockNumber = 5;
};

#pragma GCC diagnostic pop
}  // namespace bcos::test
