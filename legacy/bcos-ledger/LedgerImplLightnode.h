#pragma once

#include "../concepts/bcos-concepts/ledger/Ledger.h"
#include "../concepts/bcos-concepts/ledger/LedgerLightnode.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-concepts/Hash.h>
#include "../concepts/bcos-concepts/storage/Storage.h"
#include "LedgerImpl.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <range/v3/range/access.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <bcos-utilities/BoostLog.h>

namespace bcos::ledger
{

static constexpr const int LIGHTNODE_MAX_REQUEST_BLOCKS_COUNT = 50;

// Lightnode-specific extensions to LedgerImpl.
// These methods have been moved to legacy because they are only used by the lightnode module.
template <bcos::crypto::hasher::Hasher Hasher, bcos::concepts::storage::Storage Storage>
class LedgerImplLightnode : public LedgerImpl<Hasher, Storage>,
                              public bcos::concepts::ledger::LedgerLightnodeBase<
                                  LedgerImplLightnode<Hasher, Storage>>
{
public:
    using Base = LedgerImpl<Hasher, Storage>;
    using Base::Base;

    // Resolve diamond ambiguity: prefer LedgerLightnodeBase's versions of LedgerBase methods
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::getBlock;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::setBlock;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::getBlockNumberByHash;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::getBlockHashByNumber;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::getABI;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::getTransactions;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::getStatus;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::setupGenesisBlock;
    using bcos::concepts::ledger::LedgerLightnodeBase<
        LedgerImplLightnode<Hasher, Storage>>::checkGenesisBlock;

    using statusInfoType = std::map<crypto::NodeIDPtr, bcos::protocol::BlockNumber>;

    crypto::NodeIDs filterSyncNodeList(
        statusInfoType const& peersStatusInfo, bcos::protocol::BlockNumber needBlockNumber)
    {
        crypto::NodeIDs requestNodeIDList;
        for (const auto& nodeStatus : peersStatusInfo)
        {
            LEDGER_LOG(INFO) << LOG_KV("nodeID", nodeStatus.first->hex())
                             << LOG_KV("blockNumber: ", nodeStatus.second)
                             << LOG_KV("，needBlockNumber: ", needBlockNumber);
            if (nodeStatus.second >= needBlockNumber)
            {
                requestNodeIDList.push_back(nodeStatus.first);
            }
        }
        LEDGER_LOG(DEBUG) << LOG_KV("requestNodeIDList size", requestNodeIDList.size());
        return requestNodeIDList;
    }

    template <bcos::concepts::ledger::DataFlag... Flags>
    task::Task<void> impl_getBlockByNodeList(
        bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block, bcos::crypto::NodeIDs const& nodeList)
    {
        try
        {
            LEDGER_LOG(INFO) << "getBlockByNodeList: " << blockNumber;
            auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);
            (co_await Base::template getBlockData<Flags>(blockNumberStr, block), ...);
        }
        catch (NotFoundBlockHeader& e)
        {
            LEDGER_LOG(ERROR) << "Not found block";
            block = {};
        }
        co_return;
    }

    task::Task<std::map<crypto::NodeIDPtr, bcos::protocol::BlockNumber>> impl_getAllPeersStatus()
    {
        std::map<crypto::NodeIDPtr, bcos::protocol::BlockNumber> allPeersStatus;
        // assert(false); //never reach here
        co_return allPeersStatus;
    }

    template <bcos::concepts::ledger::Ledger LedgerType, bcos::concepts::block::Block BlockType>
    task::Task<size_t> impl_sync(LedgerType& source, bool onlyHeader)
    {
        auto& sourceLedger = bcos::concepts::getRef(source);
        auto status = co_await Base::impl_getStatus();
        auto allPeersStatus = co_await sourceLedger.getAllPeersStatus();
        bcos::protocol::BlockNumber currentMaxBlockNumber = 0;
        for (const auto& nodeStatus : allPeersStatus)
        {
            if (nodeStatus.second > currentMaxBlockNumber)
            {
                currentMaxBlockNumber = nodeStatus.second;
            }
        }
        LEDGER_LOG(DEBUG) << LOG_KV("allPeersStatus", allPeersStatus.size())
                          << LOG_KV("currentMaxBlockNumber", currentMaxBlockNumber);
        std::optional<BlockType> parentBlock;
        size_t syncedBlock = 0;
        auto syncBlockNumber = status.blockNumber + LIGHTNODE_MAX_REQUEST_BLOCKS_COUNT;
        if (allPeersStatus.size() != 0 && currentMaxBlockNumber < syncBlockNumber)
        {
            syncBlockNumber = currentMaxBlockNumber;
        }
        // sync block
        for (auto blockNumber = status.blockNumber + 1; blockNumber <= syncBlockNumber;
            ++blockNumber)
        {
            LEDGER_LOG(INFO) << "Syncing block from remote: " << blockNumber << " | "
                             << syncBlockNumber << " | " << onlyHeader;
            BlockType block;
            auto syncNodeList = filterSyncNodeList(allPeersStatus, blockNumber);
            if (onlyHeader)
            {
                co_await sourceLedger.template getBlockByNodeList<bcos::concepts::ledger::HEADER>(
                    blockNumber, block, syncNodeList);
            }
            else
            {
                co_await sourceLedger.template getBlockByNodeList<bcos::concepts::ledger::ALL>(
                    blockNumber, block, syncNodeList);
            }
            // if getBlockByNodeList return empty block, break
            if (::ranges::empty(block.blockHeader.data.parentInfo))
            {
                LEDGER_LOG(WARNING)
                    << LOG_DESC("No blockHeader in block") << LOG_KV("blockNumber", blockNumber);
                break;
            }
            if (blockNumber > 0)
            {
                if (!parentBlock)
                {
                    parentBlock = BlockType();
                    co_await Base::template impl_getBlock<bcos::concepts::ledger::HEADER>(
                        blockNumber - 1, *parentBlock);
                }
                Base::checkParentBlock(*parentBlock, block);
            }
            if (onlyHeader)
            {
                co_await Base::template impl_setBlock<bcos::concepts::ledger::HEADER>(block);
            }
            else
            {
                co_await Base::template impl_setBlock<bcos::concepts::ledger::ALL>(block);
            }
            parentBlock = std::move(block);
            ++syncedBlock;
        }
        co_return syncedBlock;
    }
};

}  // namespace bcos::ledger
