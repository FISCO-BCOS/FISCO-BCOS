#include "LedgerInitializer.h"
#include "AccountTableMigration.h"
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-task/Wait.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>
#include <bcos-utilities/BoostLog.h>
#include <legacy/bcos-ledger/LedgerImpl.h>
#include <legacy/bcos-storage/StorageWrapperImpl.h>
#include <boost/throw_exception.hpp>
#include <future>
#include <tuple>

std::shared_ptr<bcos::ledger::Ledger> bcos::initializer::LedgerInitializer::build(
    bcos::protocol::BlockFactory::Ptr blockFactory, bcos::storage::StorageInterface::Ptr storage,
    bcos::tool::NodeConfig::Ptr nodeConfig, bcos::storage::StorageInterface::Ptr blockStorage,
    bcos::IOServicePool::Ptr ioServicePool, std::optional<AccountTableBoot> accountTableBoot)
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

    // Publish the node-local account-table encoding BEFORE buildGenesisBlock: genesis state
    // loading (Ledger::importGenesisState / GenesisStateLoader) routes account-table names
    // through nodeAddressTableMode(), so a fresh chain must be born in the resolved encoding.
    if (accountTableBoot.has_value())
    {
        // Lane determination without a genesis block: on an existing chain the committed
        // rows answer executor_version; on a fresh chain the genesis config is the only
        // source. Only the legacy v0 lane is hex-only — the Eth/OP lanes are mode-aware
        // (account::ethLaneAccountTableName), so the L2 feature no longer plays a role.
        auto const onChain = readOnChainExecutorVersion(*ledger, nodeConfig->executorVersion());
        bool const hexOnlyLane = isHexOnlyExecutorLane(onChain.version);

        // The layout state machine is one key inside the state DB
        // (ACCOUNT_TABLE_LAYOUT_KEY — AddressTableModeDetection.h): absent = a pre-flag
        // hex chain or a brand-new DB, "migrating" = an unfinished migration, "bin" = a
        // binary-layout DB. Reading it is one point Get — no registration scan on the
        // steady-state boot path. The flag decides first: "bin" publishes Binary and the
        // migration switch is ignored; only WITHOUT the flag does the switch get a say —
        // it resumes an unfinished migration ("migrating", idempotent) or runs the
        // one-shot rewrite. With the switch off, resolveNodeAddressTableMode refuses to
        // start on "migrating", with the recovery instructions. Hex-only lanes are
        // refused inside migrateAccountTablesToBinary.
        auto& stateDB = accountTableBoot->stateDB.get();
        auto layoutFlag = readAccountTableLayoutFlag(stateDB);
        if (accountTableBoot->migrateToBinary &&
            (!layoutFlag.has_value() || *layoutFlag != ACCOUNT_TABLE_LAYOUT_BINARY))
        {
            if (layoutFlag.has_value())
            {
                BCOS_LOG(WARNING) << LOG_BADGE("LedgerInitializer")
                                  << LOG_DESC(
                                         "unfinished hex->binary account-table migration detected "
                                         "(layout flag \"migrating\"); resuming it now")
                                  << LOG_KV("key", ACCOUNT_TABLE_LAYOUT_KEY);
            }
            auto const stats = migrateAccountTablesToBinary(stateDB, hexOnlyLane);
            BCOS_LOG(INFO) << LOG_BADGE("LedgerInitializer")
                           << LOG_DESC("account-table migration finished")
                           << LOG_KV("alreadyMigrated", stats.alreadyMigrated)
                           << LOG_KV("accountRows", stats.migratedAccountRows)
                           << LOG_KV("registrations", stats.migratedRegistrations)
                           << LOG_KV("deduped", stats.dedupedRows);
            layoutFlag = readAccountTableLayoutFlag(stateDB);
        }

        bool const chainHasState = hasAnyTableRegistration(stateDB);
        // Cross-check the flag-absent Hex verdict with one bounded probe: a binary-layout
        // DB whose flag was lost must refuse to boot, not publish Hex over data it cannot
        // read (no-op when the flag is present).
        refuseBinaryDataWithoutFlag(stateDB, layoutFlag);
        auto const mode = resolveNodeAddressTableMode(layoutFlag, hexOnlyLane, chainHasState);
        if (mode == ledger::account::AddressTableMode::Binary && !layoutFlag.has_value())
        {
            // A brand-new chain born binary: persist the verdict BEFORE buildGenesisBlock,
            // so binary tables never exist without the flag — a crash in between would
            // otherwise read as a pre-flag hex chain and boot Hex over a binary genesis.
            writeAccountTableLayoutFlag(stateDB, ACCOUNT_TABLE_LAYOUT_BINARY);
        }
        bcos::ledger::account::setNodeAddressTableMode(mode);
        BCOS_LOG(INFO) << LOG_BADGE("LedgerInitializer")
                       << LOG_DESC("node-local account-table encoding")
                       << LOG_KV("mode", magic_enum::enum_name(mode))
                       << LOG_KV("layoutFlag", layoutFlag.value_or("<absent>"))
                       << LOG_KV("chainHasState", chainHasState)
                       << LOG_KV("hexOnlyLane", hexOnlyLane)
                       << LOG_KV("executorVersion", onChain.version);
    }

    ledger->buildGenesisBlock(nodeConfig->genesisConfig(), *nodeConfig->ledgerConfig());

    // OP mode is a genesis-only property: executor_version >= OPSTACK must be genesis-bound
    // (activation block 0). The value is read from the ledger (written at genesis), with the
    // genesis config as the fallback when the on-chain entry is absent. The Ethereum lane
    // (executor_version >= ETHEREUM) implies the Ethereum state shape — MPT from genesis,
    // /apps/ naming for every address — with no separate feature flag.
    // A value above the newest declared lane is not refused here: the scheduler saturates it
    // onto the newest wired slot (MultiVersionScheduler::setVersion), and refusing it at boot
    // would strand a chain that wrote such a row before 3.18 with no way to lower it.
    {
        auto const onChain = readOnChainExecutorVersion(*ledger, nodeConfig->executorVersion());
        bcos::scheduler_v1::validateOpModeGenesisOnly(onChain.version, onChain.activation);
    }

    return ledger;
}

bcos::initializer::OnChainExecutorVersion bcos::initializer::readOnChainExecutorVersion(
    bcos::ledger::LedgerInterface& ledger, int fallbackVersion)
{
    OnChainExecutorVersion result{.version = fallbackVersion};
    auto const raw = bcos::task::syncWait(bcos::ledger::getSystemConfig(
        ledger, magic_enum::enum_name(bcos::ledger::SystemConfig::executor_version)));
    if (!raw.has_value())
    {
        return result;
    }
    try
    {
        result.version = boost::lexical_cast<int>(std::get<0>(*raw));
    }
    catch (boost::bad_lexical_cast const&)
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "on-chain executor_version is not an integer: '" + std::get<0>(*raw) + "'"));
    }
    result.activation = std::get<1>(*raw);
    result.present = true;
    return result;
}
