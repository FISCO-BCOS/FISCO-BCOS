#include "LedgerMethods.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/SystemConfigs.h"
#include "bcos-tool/VersionConverter.h"
#include <bcos-executor/src/Common.h>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <functional>

bcos::task::Task<void> bcos::ledger::prewriteBlockToStorage(LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, storage::StorageInterface::Ptr storage,
    std::optional<bcos::crypto::HashType> blockHashOverride, bool writeNonces)
{
    struct Awaitable
    {
        std::reference_wrapper<LedgerInterface> m_ledger;
        decltype(transactions) m_transactions;
        decltype(block) m_block;
        bool m_withTransactionsAndReceipts{};
        decltype(storage) m_storage;
        std::optional<bcos::crypto::HashType> m_blockHashOverride;
        bool m_writeNonces{true};
        Error::Ptr m_error;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.get().asyncPrewriteBlock(
                m_storage, std::move(m_transactions), std::move(m_block),
                [this, handle](std::string, Error::Ptr error) {
                    if (error)
                    {
                        m_error = std::move(error);
                    }
                    handle.resume();
                },
                m_withTransactionsAndReceipts, std::nullopt, m_blockHashOverride, m_writeNonces);
        }
        void await_resume()
        {
            if (m_error)
            {
                BOOST_THROW_EXCEPTION(*m_error);
            }
        }
    };

    Awaitable awaitable{.m_ledger = ledger,
        .m_transactions = std::move(transactions),
        .m_block = std::move(block),
        .m_withTransactionsAndReceipts = withTransactionsAndReceipts,
        .m_storage = std::move(storage),
        .m_blockHashOverride = std::move(blockHashOverride),
        .m_writeNonces = writeNonces,
        .m_error = {}};
    co_await awaitable;
}

bcos::task::Task<void> bcos::ledger::tag_invoke(
    ledger::tag_t<storeTransactionsAndReceipts> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block)
{
    auto error = ledger.storeTransactionsAndReceipts(std::move(blockTxs), std::move(block));
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }
    co_return;
}

void bcos::ledger::tag_invoke(ledger::tag_t<removeExpiredNonce> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber expiredNumber)
{
    ledger.removeExpiredNonce(expiredNumber, false);
}

bcos::task::Task<bcos::protocol::Block::Ptr> bcos::ledger::tag_invoke(
    ledger::tag_t<getBlockData> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber blockNumber, int32_t blockFlag)
{
    struct Awaitable
    {
        std::reference_wrapper<LedgerInterface> m_ledger;
        protocol::BlockNumber m_blockNumber;
        int32_t m_blockFlag;

        std::variant<Error::Ptr, bcos::protocol::Block::Ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.get().asyncGetBlockDataByNumber(m_blockNumber, m_blockFlag,
                [this, handle](Error::Ptr error, bcos::protocol::Block::Ptr block) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<bcos::protocol::Block::Ptr>(std::move(block));
                    }
                    handle.resume();
                });
        }
        bcos::protocol::Block::Ptr await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<bcos::protocol::Block::Ptr>(m_result));
        }
    };
    Awaitable awaitable{
        .m_ledger = ledger, .m_blockNumber = blockNumber, .m_blockFlag = blockFlag, .m_result = {}};

    co_return co_await awaitable;
}

bcos::task::Task<bcos::ledger::TransactionCount> bcos::ledger::tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger)
{
    struct Awaitable
    {
        std::reference_wrapper<LedgerInterface> m_ledger;
        std::variant<Error::Ptr, TransactionCount> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.get().asyncGetTotalTransactionCount(
                [this, handle](Error::Ptr error, int64_t total, int64_t failed,
                    bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<TransactionCount>(TransactionCount{
                            .total = total,
                            .failed = failed,
                            .blockNumber = blockNumber,
                        });
                    }
                    handle.resume();
                });
        }
        TransactionCount await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<TransactionCount>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_result = {}};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::protocol::BlockNumber> bcos::ledger::tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger)
{
    struct Awaitable
    {
        std::reference_wrapper<LedgerInterface> m_ledger;
        std::variant<Error::Ptr, protocol::BlockNumber> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.get().asyncGetBlockNumber(
                [this, handle](Error::Ptr error, bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<protocol::BlockNumber>(blockNumber);
                    }
                    handle.resume();
                });
        }
        protocol::BlockNumber await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<protocol::BlockNumber>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_result = {}};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::crypto::HashType> bcos::ledger::tag_invoke(
    ledger::tag_t<getBlockHash> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber blockNumber)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        protocol::BlockNumber m_blockNumber;

        std::variant<Error::Ptr, crypto::HashType> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockHashByNumber(
                m_blockNumber, [this, handle](Error::Ptr error, crypto::HashType hash) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<crypto::HashType>(hash);
                    }
                    handle.resume();
                });
        }
        crypto::HashType await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<crypto::HashType>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_blockNumber = blockNumber, .m_result = {}};
    co_return co_await awaitable;
}

bcos::task::Task<bcos::protocol::BlockNumber> bcos::ledger::tag_invoke(
    bcos::ledger::tag_t<bcos::ledger::getBlockNumber> /*unused*/,
    bcos::ledger::LedgerInterface& ledger, bcos::crypto::HashType hash)
{
    struct Awaitable
    {
        bcos::ledger::LedgerInterface& m_ledger;
        bcos::crypto::HashType m_hash;

        std::variant<bcos::Error::Ptr, bcos::protocol::BlockNumber> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockNumberByHash(
                m_hash, [this, handle](bcos::Error::Ptr error, bcos::protocol::BlockNumber number) {
                    if (error)
                    {
                        m_result.emplace<bcos::Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<bcos::protocol::BlockNumber>(number);
                    }
                    handle.resume();
                });
        }
        bcos::protocol::BlockNumber await_resume()
        {
            if (std::holds_alternative<bcos::Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<bcos::Error::Ptr>(m_result));
            }
            return std::get<bcos::protocol::BlockNumber>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_hash = hash, .m_result = {}};
    co_return co_await awaitable;
}

bcos::task::Task<std::optional<bcos::ledger::SystemConfigEntry>> bcos::ledger::tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, LedgerInterface& ledger, std::string_view key)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::string_view m_key;
        std::variant<Error::Ptr, std::optional<SystemConfigEntry>> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetSystemConfigByKey(
                m_key, [this, handle](Error::Ptr error, std::string value,
                           bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        if (error->errorCode() == LedgerError::EmptyEntry ||
                            error->errorCode() == LedgerError::ErrorArgument ||
                            error->errorCode() == LedgerError::GetStorageError)
                        {
                            m_result.emplace<std::optional<SystemConfigEntry>>();
                        }
                        else
                        {
                            m_result.emplace<Error::Ptr>(std::move(error));
                        }
                    }
                    else
                    {
                        m_result.emplace<std::optional<SystemConfigEntry>>(
                            std::in_place, std::move(value), blockNumber);
                    }
                    handle.resume();
                });
        }
        std::optional<SystemConfigEntry> await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<std::optional<SystemConfigEntry>>(m_result));
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_key = key, .m_result = {}};
    co_return co_await awaitable;
}

bcos::task::Task<bcos::consensus::ConsensusNodeList> bcos::ledger::tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, LedgerInterface& ledger, std::string_view type)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::string_view m_type;
        std::variant<Error::Ptr, consensus::ConsensusNodeList> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetNodeListByType(m_type,
                [this, handle](Error::Ptr error, consensus::ConsensusNodeList consensusNodeList) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<consensus::ConsensusNodeList>(
                            std::move(consensusNodeList));
                    }
                    handle.resume();
                });
        }
        consensus::ConsensusNodeList await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<consensus::ConsensusNodeList>(m_result));
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_type = type, .m_result = {}};
    co_return co_await awaitable;
}

static bcos::task::Task<std::tuple<std::string, bcos::protocol::BlockNumber>>
getSystemConfigOrDefault(
    bcos::ledger::LedgerInterface& ledger, std::string_view key, std::string defaultValue)
{
    try
    {
        auto config = co_await bcos::ledger::getSystemConfig(ledger, key);
        if (!config)
        {
            LEDGER2_LOG(DEBUG) << "Get " << key << " failed, use default value"
                               << LOG_KV("defaultValue", defaultValue);
            co_return std::tuple<std::string, bcos::protocol::BlockNumber>{defaultValue, 0};
        }
        auto [value, blockNumber] = *config;
        co_return std::tuple<std::string, bcos::protocol::BlockNumber>{value, blockNumber};
    }
    catch (std::exception& e)
    {
        LEDGER2_LOG(DEBUG) << "Get " << key << " failed, use default value"
                           << LOG_KV("defaultValue", defaultValue);
        co_return std::tuple<std::string, bcos::protocol::BlockNumber>{defaultValue, 0};
    }
}

static bcos::task::Task<std::tuple<int64_t, bcos::protocol::BlockNumber>> getSystemConfigOrDefault(
    bcos::ledger::LedgerInterface& ledger, std::string_view key, int64_t defaultValue)
{
    auto [value, blockNumber] = co_await getSystemConfigOrDefault(ledger, key, "");
    if (value.empty())
    {
        co_return std::make_tuple(defaultValue, 0);
    }
    co_return std::make_tuple(boost::lexical_cast<int64_t>(value), blockNumber);
}

bcos::task::Task<void> bcos::ledger::tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger, LedgerConfig& ledgerConfig)
{
    auto nodeList = co_await getNodeList(ledger, {});
    ledgerConfig.setConsensusNodeList(::ranges::views::filter(nodeList, [](auto& node) {
        return node.type == consensus::Type::consensus_sealer;
    }) | ::ranges::to<std::vector>());
    ledgerConfig.setObserverNodeList(::ranges::views::filter(nodeList, [](auto& node) {
        return node.type == consensus::Type::consensus_observer;
    }) | ::ranges::to<std::vector>());

    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    auto sysConfig = co_await ledger.fetchAllSystemConfigs(blockNumber + 1);

    if (auto txLimitConfig = sysConfig.get(ledger::SystemConfig::tx_count_limit))
    {
        ledgerConfig.setBlockTxCountLimit(
            boost::lexical_cast<uint64_t>(txLimitConfig.value().first));
    }
    if (auto ledgerSwitchPeriodConfig =
            sysConfig.get(ledger::SystemConfig::consensus_leader_period))
    {
        ledgerConfig.setLeaderSwitchPeriod(
            boost::lexical_cast<uint64_t>(ledgerSwitchPeriodConfig.value().first));
    }
    auto txGasLimit = sysConfig.getOrDefault(ledger::SystemConfig::tx_gas_limit, "0");
    ledgerConfig.setGasLimit({boost::lexical_cast<uint64_t>(txGasLimit.first), txGasLimit.second});

    if (auto versionConfig = sysConfig.get(ledger::SystemConfig::compatibility_version))
    {
        ledgerConfig.setCompatibilityVersion(tool::toVersionNumber(versionConfig.value().first));
    }
    auto gasPrice = sysConfig.getOrDefault(ledger::SystemConfig::tx_gas_price, "0x0");
    ledgerConfig.setGasPrice(std::make_tuple(gasPrice.first, gasPrice.second));

    // Excess blob gas (EIP-4844) — consumed only by the pure-Ethereum EthereumExecutor
    // (executor_version=2) to derive the blob base fee. Persisted at genesis by
    // Ledger::buildGenesisBlock from tx.excess_blob_gas; when absent the executor defaults
    // to an excess of 0 (blob base fee 1).
    if (auto excessBlobGas = sysConfig.get(ledger::SystemConfig::excess_blob_gas); excessBlobGas)
    {
        ledgerConfig.setExcessBlobGas(boost::lexical_cast<uint64_t>(excessBlobGas.value().first));
    }

    // Get block header to retrieve timestamp
    auto block = co_await getBlockData(ledger, blockNumber, HEADER);
    if (block && block->blockHeader())
    {
        ledgerConfig.setTimestamp(block->blockHeader()->timestamp());
        // ledgerConfig.setHash(block->blockHeader()->hash());
    }
    ledgerConfig.setBlockNumber(blockNumber);
    ledgerConfig.setHash(co_await getBlockHash(ledger, blockNumber));
    ledgerConfig.setFeatures(co_await getFeatures(ledger));

    auto enableRPBFT =
        (sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft, "0").first == "1");
    ledgerConfig.setConsensusType(
        std::string(enableRPBFT ? ledger::RPBFT_CONSENSUS_TYPE : ledger::PBFT_CONSENSUS_TYPE));
    if (enableRPBFT)
    {
        ledgerConfig.setCandidateSealerNodeList(::ranges::views::filter(nodeList, [](auto& node) {
            return node.type == consensus::Type::consensus_candidate_sealer;
        }) | ::ranges::to<std::vector>());

        auto epochSealer =
            sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft_epoch_sealer_num,
                std::to_string(DEFAULT_EPOCH_SEALER_NUM));
        ledgerConfig.setEpochSealerNum(
            {boost::lexical_cast<uint64_t>(epochSealer.first), epochSealer.second});

        auto epochBlock =
            sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft_epoch_block_num,
                std::to_string(DEFAULT_EPOCH_BLOCK_NUM));
        ledgerConfig.setEpochBlockNum(
            {boost::lexical_cast<uint64_t>(epochBlock.first), epochBlock.second});
        ledgerConfig.setNotifyRotateFlagInfo(std::get<0>(co_await getSystemConfigOrDefault(
            ledger, INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, DEFAULT_INTERNAL_NOTIFY_FLAG)));
    }
    auto auth = sysConfig.getOrDefault(ledger::SystemConfig::auth_check_status, "0");
    ledgerConfig.setAuthCheckStatus(boost::lexical_cast<uint32_t>(auth.first));
    auto [chainId, _] = sysConfig.getOrDefault(ledger::SystemConfig::web3_chain_id, "0");
    // Fail-stop on a malformed value (InvalidWeb3ChainIdConfig), same policy as
    // evmc_revision below: CHAINID is contract-visible execution semantics and the
    // admission side already rejects the same value, so silently serving 0 is a
    // silent-divergence hazard. Absent config arrives as the "0" default and parses fine.
    ledgerConfig.setChainId(bcos::toEvmC(ledger::parseConfiguredWeb3ChainId(chainId)));
    ledgerConfig.setBalanceTransfer(
        sysConfig.getOrDefault(ledger::SystemConfig::balance_transfer, "0").first != "0");

    int executorVersion = 0;
    if (auto versionConfig = sysConfig.get(ledger::SystemConfig::executor_version); versionConfig)
    {
        executorVersion = boost::lexical_cast<int>(versionConfig.value().first);
        ledgerConfig.setExecutorVersion(executorVersion);
    }

    // EVMC revision — consumed only by the pure-Ethereum EthereumExecutor
    // (executor_version=2); v0/v1 schedulers never read evmcRevision()/evmcRevisionForBlock(),
    // so a non-v2 chain is left untouched (no implicit default injection, which would be an
    // unnoticed behavior change if a future v0/v1 path started reading it). For v2, an
    // explicitly configured revision was persisted at genesis (Ledger::buildGenesisBlock);
    // the fallback here covers a v2 genesis without one (defensive — NodeConfig::loadExecutorConfig
    // requires an explicit revision for executor_version=2, so this default only fires on
    // corrupt/legacy state).
    //
    // No per-call logging here: getLedgerConfig sits on the per-block / per-RPC hot path.
    // The effective revision is parsed and logged once at startup (Initializer), which the
    // CI pins and where a corrupt value becomes a boot refusal.
    if (executorVersion >= ledger::ETHEREUM_EXECUTOR_VERSION)
    {
        if (auto evmcRevision = sysConfig.get(ledger::SystemConfig::evmc_revision); evmcRevision)
        {
            // A corrupt persisted value halts loudly (InvalidEVMCRevisionConfig) instead of
            // silently running a compile-time default that could differ between binaries.
            ledger::applyEVMCRevisionConfig(ledgerConfig, evmcRevision.value().first);
        }
        else
        {
            ledgerConfig.setEVMCRevision(ledger::EVMC_REVISION_DEFAULT);
        }
    }
}

bcos::task::Task<bcos::ledger::Features> bcos::ledger::tag_invoke(
    ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger)
{
    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    Features features;
    try
    {
        features = co_await ledger.fetchAllFeatures(blockNumber + 1);
    }
    catch (...)
    {
        LEDGER2_LOG(DEBUG) << LOG_DESC("fetch features failed")
                           << LOG_KV("msg", boost::current_exception_diagnostic_information());
    }

    co_return features;
}

bcos::task::Task<bool> bcos::ledger::tag_invoke(ledger::tag_t<getFeature> /*unused*/,
    LedgerInterface& ledger, ledger::Features::Flag flag, protocol::BlockNumber blockNumber)
{
    // Single-flag read: Ledger overrides fetchFeature with one SYS_CONFIG row instead of
    // fetchAllFeatures' scan of every feature key (~60 rows). Used by the historical
    // state-read path (feature_l2_ethereum_compat) which needs exactly one flag; degrades
    // to false (scenario A) on any failure, the same honest default as getFeatures'
    // empty-set fallback.
    try
    {
        co_return co_await ledger.fetchFeature(flag, blockNumber);
    }
    catch (...)
    {
        LEDGER2_LOG(DEBUG) << LOG_DESC("fetch feature failed")
                           << LOG_KV("flag", magic_enum::enum_name(flag))
                           << LOG_KV("msg", boost::current_exception_diagnostic_information());
        co_return false;
    }
}

bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> bcos::ledger::tag_invoke(
    ledger::tag_t<getReceipt> /*unused*/, LedgerInterface& ledger, crypto::HashType const& txHash)
{
    struct Awaitable
    {
        bcos::ledger::LedgerInterface& m_ledger;
        bcos::crypto::HashType m_hash;

        std::variant<bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetTransactionReceiptByHash(m_hash, false,
                [this, handle](bcos::Error::Ptr error,
                    const bcos::protocol::TransactionReceipt::Ptr& receipt, MerkleProofPtr) {
                    if (error)
                    {
                        m_result.emplace<bcos::Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<bcos::protocol::TransactionReceipt::Ptr>(receipt);
                    }
                    handle.resume();
                });
        }
        bcos::protocol::TransactionReceipt::Ptr await_resume()
        {
            if (std::holds_alternative<bcos::Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<bcos::Error::Ptr>(m_result));
            }
            return std::get<bcos::protocol::TransactionReceipt::Ptr>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_hash = txHash, .m_result = {}};
    co_return co_await awaitable;
}

bcos::task::Task<bcos::protocol::TransactionsConstPtr> bcos::ledger::tag_invoke(
    ledger::tag_t<getTransactions> /*unused*/, LedgerInterface& ledger, crypto::HashListPtr hashes)
{
    struct Awaitable
    {
        bcos::ledger::LedgerInterface& m_ledger;
        bcos::crypto::HashListPtr m_hashes;

        std::variant<bcos::Error::Ptr, bcos::protocol::TransactionsConstPtr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBatchTxsByHashList(
                std::move(m_hashes), false, [this, handle](auto&& error, auto&& txs, auto&&) {
                    if (error)
                    {
                        m_result.emplace<bcos::Error::Ptr>(std::forward<decltype(error)>(error));
                    }
                    else
                    {
                        m_result.emplace<bcos::protocol::TransactionsConstPtr>(txs);
                    }
                    handle.resume();
                });
        }
        bcos::protocol::TransactionsConstPtr await_resume()
        {
            if (std::holds_alternative<bcos::Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<bcos::Error::Ptr>(m_result));
            }
            return std::get<bcos::protocol::TransactionsConstPtr>(m_result);
        }
    };

    Awaitable awaitable{.m_ledger = ledger, .m_hashes = std::move(hashes), .m_result = {}};
    co_return co_await awaitable;
}

bcos::task::Task<std::optional<bcos::storage::Entry>> bcos::ledger::tag_invoke(
    ledger::tag_t<getStorageAt> /*unused*/, LedgerInterface& ledger, std::string_view address,
    std::string_view key, bcos::protocol::BlockNumber number)
{
    co_return co_await ledger.getStorageAt(address, key, number);
}

bcos::task::Task<
    std::shared_ptr<std::map<bcos::protocol::BlockNumber, bcos::protocol::NonceListPtr>>>
bcos::ledger::tag_invoke(ledger::tag_t<getNonceList> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::BlockNumber startNumber, int64_t offset)
{
    struct Awaitable
    {
        bcos::ledger::LedgerInterface& m_ledger;
        bcos::protocol::BlockNumber m_startNumber;
        int64_t m_offset;

        std::variant<bcos::Error::Ptr,
            std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>>
            m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_ledger.asyncGetNonceList(m_startNumber, m_offset,
                [this, handle](auto&& error,
                    std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>
                        nonceList) {
                    if (error)
                    {
                        m_result.emplace<bcos::Error::Ptr>(std::forward<decltype(error)>(error));
                    }
                    else
                    {
                        m_result.emplace<decltype(nonceList)>(std::move(nonceList));
                    }
                    handle.resume();
                });
        }
        std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>> await_resume()
        {
            if (std::holds_alternative<bcos::Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<bcos::Error::Ptr>(m_result));
            }
            return std::get<
                std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>>(m_result);
        }
    };

    Awaitable awaitable{
        .m_ledger = ledger, .m_startNumber = startNumber, .m_offset = offset, .m_result = {}};
    co_return co_await awaitable;
}
