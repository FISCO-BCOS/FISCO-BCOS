#include "LedgerInitializer.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>
#include <legacy/bcos-ledger/LedgerImpl.h>
#include <legacy/bcos-storage/StorageWrapperImpl.h>
#include <future>
#include <tuple>

std::shared_ptr<bcos::ledger::Ledger> bcos::initializer::LedgerInitializer::build(
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::storage::StorageInterface::Ptr storage,
    bcos::tool::NodeConfig::Ptr nodeConfig, bcos::storage::StorageInterface::Ptr blockStorage,
    bcos::IOServicePool::Ptr ioServicePool)
{
    bcos::storage::StorageImpl storageWrapper(storage);
    std::shared_ptr<bcos::ledger::Ledger> ledger;
    if (nodeConfig->smCryptoType())
    {
        ledger = std::make_shared<bcos::ledger::LedgerImpl<
            bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher, decltype(storageWrapper)>>(
            bcos::crypto::hasher::openssl::OpenSSL_SM3_Hasher{}, std::move(storageWrapper),
            blockFactory, storage, nodeConfig->blockLimit(), blockStorage);
    }
    else
    {
        ledger = std::make_shared<bcos::ledger::LedgerImpl<
            bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher, decltype(storageWrapper)>>(
            bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher{}, std::move(storageWrapper),
            blockFactory, storage, nodeConfig->blockLimit(), blockStorage);
    }

    ledger->buildGenesisBlock(nodeConfig->genesisConfig(), *nodeConfig->ledgerConfig());

    // Startup MPT flag-matrix guard (spec 5.10 scenario B, M7.3): refuse to boot when
    // feature_l2_ethereum_compat carries a non-genesis activation block — enabling it
    // mid-chain would switch the state-root scheme with no transition rule and fork any
    // node replaying the pre-flag blocks. Validated against the feature set the NEXT
    // block runs with (head + 1), the same height convention as the commit path.
    std::promise<std::tuple<bcos::Error::Ptr, bcos::protocol::BlockNumber>> blockNumberPromise;
    ledger->asyncGetBlockNumber([&](bcos::Error::Ptr error, bcos::protocol::BlockNumber number) {
        blockNumberPromise.set_value(std::make_tuple(std::move(error), number));
    });
    auto [error, blockNumber] = blockNumberPromise.get_future().get();
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }
    bcos::scheduler_v1::validateMPTFlagMatrix(
        bcos::task::syncWait(ledger->fetchAllFeatures(blockNumber + 1)));

    return ledger;
}
