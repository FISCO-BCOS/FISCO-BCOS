#include "LedgerInitializer.h"
#include "bcos-ledger/LedgerImpl.h"
#include "bcos-storage/bcos-storage/StorageWrapperImpl.h"

std::shared_ptr<bcos::ledger::Ledger> bcos::initializer::LedgerInitializer::build(
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::storage::StorageInterface::Ptr storage,
    bcos::tool::NodeConfig::Ptr nodeConfig, bcos::storage::StorageInterface::Ptr blockStorage)
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
    return ledger;
}
