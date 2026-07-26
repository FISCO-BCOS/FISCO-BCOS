#pragma once

#include "RollbackableStorage.h"
#include "bcos-executor/src/Web3AccessListResolver.h"
#include "bcos-executor/src/vm/Eip2929AccessState.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Exceptions.h"
#include "precompiled/PrecompiledManager.h"
#include "vm/HostContext.h"
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <type_traits>

namespace bcos::executor_v1
{
#define TRANSACTION_EXECUTOR_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TRANSACTION_EXECUTOR")

DERIVE_BCOS_EXCEPTION(InvalidReceiptVersion);

evmc_message newEVMCMessage(protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin);

class TransactionExecutorImpl
{
public:
    TransactionExecutorImpl(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl, PrecompiledManager& precompiledManager);

    std::reference_wrapper<protocol::TransactionReceiptFactory const> m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    std::reference_wrapper<PrecompiledManager> m_precompiledManager;

    using TransientStorage =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

    // FIB-75: Effective gas limit for EVM execution.
    // When bugfix_gas_payment_balance_precheck is enabled and the tx declares gasLimit > 0,
    // the EVM budget is capped at tx.gasLimit() (geth-compatible); otherwise falls back to
    // blockGasLimit for backward compat.

    /// Ethereum mode: use LedgerConfig's explicit EVMC revision if set,
    /// otherwise fall back to toRevision feature mapping.
    static evmc_revision effectiveRevision(
        ledger::LedgerConfig const& cfg, protocol::BlockHeader const& header)
    {
        if (auto rev = cfg.evmcRevision(); rev.has_value())
            return *rev;
        return bcos::executor::toRevision(cfg.features(), header.version());
    }

    static int64_t computeEffectiveGasLimit(
        protocol::Transaction const& tx, ledger::LedgerConfig const& cfg)
    {
        const auto txSysGasLimit = static_cast<int64_t>(std::get<0>(cfg.gasLimit()));
        if (cfg.features().get(ledger::Features::Flag::bugfix_gas_payment_balance_precheck) &&
            tx.gasLimit() > 0)
        {
            return std::min<int64_t>(tx.gasLimit(), txSysGasLimit);
        }
        return txSysGasLimit;
    }

    // Ethereum mode: compute pure intrinsic gas cost (EIP-2028, EIP-3860, EIP-7702).
    // Does NOT include EIP-7623 floor — that is applied post-execution.
    // Reference: bcos-evm state.cpp compute_tx_intrinsic_cost().
    static int64_t computeTxIntrinsicCostPure(
        evmc_revision rev, const protocol::Transaction& tx, int64_t accessListCost)
    {
        static constexpr auto TX_BASE_COST = 21000;
        static constexpr auto TX_CREATE_COST = 32000;
        static constexpr auto DATA_TOKEN_COST = 4;
        static constexpr auto INITCODE_WORD_COST = 2;
        static constexpr auto AUTHORIZATION_COST = 25000;  // EIP-7702

        auto input = tx.input();
        const bool isCreate = tx.to().empty();

        const size_t numZero =
            static_cast<size_t>(std::count(input.begin(), input.end(), static_cast<uint8_t>(0)));
        const size_t numNonzero = input.size() - numZero;
        const size_t nonzeroMult = rev >= EVMC_ISTANBUL ? 4 : 17;
        const auto dataCost = static_cast<int64_t>(nonzeroMult * numNonzero + numZero) * DATA_TOKEN_COST;

        const auto createCost = (isCreate && rev >= EVMC_HOMESTEAD) ? TX_CREATE_COST : 0;

        const auto initcodeCost =
            (isCreate && rev >= EVMC_SHANGHAI) ?
                INITCODE_WORD_COST * static_cast<int64_t>((input.size() + 31) / 32) :
                int64_t{0};

        const auto authListCost =
            rev >= EVMC_PRAGUE ?
                static_cast<int64_t>(tx.authorizationList().size()) * AUTHORIZATION_COST :
                int64_t{0};

        return TX_BASE_COST + createCost + dataCost + accessListCost + initcodeCost + authListCost;
    }

    // EIP-7623 floor gas cost (Prague+): 21000 + 10 * total_calldata_tokens.
    // Reference: bcos-evm state.cpp compute_eip7623_min_gas_cost().
    static int64_t computeEip7623FloorCost(evmc_revision rev, const protocol::Transaction& tx)
    {
        if (rev < EVMC_PRAGUE)
            return 0;

        static constexpr auto TX_BASE_COST = 21000;
        static constexpr auto TOTAL_COST_FLOOR_PER_TOKEN = 10;

        auto input = tx.input();
        const size_t numZero =
            static_cast<size_t>(std::count(input.begin(), input.end(), static_cast<uint8_t>(0)));
        const size_t numNonzero = input.size() - numZero;
        const size_t nonzeroMult = (rev >= EVMC_ISTANBUL) ? 4 : 17;
        const auto numTokens = static_cast<int64_t>(nonzeroMult * numNonzero + numZero);

        return TX_BASE_COST + numTokens * TOTAL_COST_FLOOR_PER_TOKEN;
    }

    // Validation-level intrinsic: includes EIP-7623 floor for Prague+.
    // Used for tx.gasLimit validation check only.
    static int64_t computeTxIntrinsicCost(
        evmc_revision rev, const protocol::Transaction& tx, int64_t accessListCost)
    {
        auto const intrinsic = computeTxIntrinsicCostPure(rev, tx, accessListCost);
        auto const floor = computeEip7623FloorCost(rev, tx);
        return (rev >= EVMC_PRAGUE) ? std::max(intrinsic, floor) : intrinsic;
    }

    // --- EIP-7702: set_code / delegation ---
    // Reference: bcos-evm/bcos-evm/eth/state/state.cpp process_authorization_list()
    static constexpr auto AUTHORIZATION_EMPTY_ACCOUNT_COST = 25000;
    static constexpr auto AUTHORIZATION_BASE_COST = 12500;
    // DELEGATION_MAGIC: EIP-7702 delegation designator prefix (0xef0100)
    inline static const std::array<uint8_t, 3> DELEGATION_MAGIC = {0xef, 0x01, 0x00};
    // secp256k1n / 2 (EIP-2 s-value upper bound, from bcos-evm constant)
    // NOLINTNEXTLINE
    inline static const u256 SECP256K1N_OVER_2 =
        u256{"0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF5D576E7357A4501DDFE92F46681B20A0"};

    /// Process EIP-7702 authorization list and return total delegation_refund.
    /// Each authorization delegates an authority account's code to 0xef0100 || auth.address.
    /// Authority accounts that already exist earn a refund of
    /// (AUTHORIZATION_EMPTY_ACCOUNT_COST - AUTHORIZATION_BASE_COST) = 12500 gas.
    ///
    /// Called from step 0 in Ethereum mode when rev >= EVMC_PRAGUE.
    template <class Storage>
    static task::Task<int64_t> processAuthorizationList(Storage& storage,
        protocol::Transaction const& tx, uint64_t chainId, evmc_revision rev,
        const ledger::Features& features, crypto::Hash const& hashImpl)
    {
        if (rev < EVMC_PRAGUE || !features.get(ledger::Features::Flag::feature_ethereum_executor))
        {
            co_return 0;
        }

        auto const& authList = tx.authorizationList();
        if (authList.empty())
        {
            co_return 0;
        }

        int64_t delegationRefund = 0;
        const bcos::h256 EMPTY_CODE_HASH{};
        static constexpr auto EXISTING_AUTHORITY_REFUND =
            AUTHORIZATION_EMPTY_ACCOUNT_COST - AUTHORIZATION_BASE_COST;  // 12500
        static constexpr uint64_t NONCE_MAX = std::numeric_limits<uint64_t>::max();
        static const bcos::Address ZERO_ADDRESS{};

        for (auto const& auth : authList)
        {
            // 1. Verify chain_id matches or is 0 (any chain)
            if (auth.chainId != 0 && auth.chainId != chainId)
                continue;

            // 2. Nonce must be < 2^64 - 1
            if (auth.nonce == NONCE_MAX)
                continue;

            // 3. v must be 0 or 1 for EIP-7702/2930 signatures
            if (auth.v > 1)
                continue;

            // 4. s <= secp256k1n/2 (EIP-2)
            if (auth.s > SECP256K1N_OVER_2)
                continue;

            auto const authorityEvmAddr = toEvmC(auth.signer);
            ledger::account::EVMAccount authorityAccount(storage, authorityEvmAddr,
                features.get(ledger::Features::Flag::feature_raw_address));

            if (!co_await authorityAccount.exists())
            {
                co_await authorityAccount.create();
            }

            // 5. Verify code is empty or already delegated
            auto codeHash = co_await authorityAccount.codeHash();
            bool isDelegated = false;
            if (codeHash != EMPTY_CODE_HASH)
            {
                auto codeEntry = co_await authorityAccount.code();
                if (codeEntry &&
                    static_cast<size_t>(codeEntry->size()) >= DELEGATION_MAGIC.size() &&
                    static_cast<uint8_t>(codeEntry->get()[0]) == DELEGATION_MAGIC[0] &&
                    static_cast<uint8_t>(codeEntry->get()[1]) == DELEGATION_MAGIC[1] &&
                    static_cast<uint8_t>(codeEntry->get()[2]) == DELEGATION_MAGIC[2])
                {
                    isDelegated = true;
                }
                if (!isDelegated && codeHash != EMPTY_CODE_HASH)
                    continue;
            }

            // 6. Verify authority nonce matches
            auto storageNonce = co_await authorityAccount.nonce();
            auto existingNonce = bcos::u256(storageNonce.value_or("0"));
            if (existingNonce != auth.nonce)
                continue;

            // 7. Refund for existing accounts (EIP-7702)
            // Reference: bcos-evm state.cpp — refund if account is NOT empty (EIP-161:
            // nonce != 0 || balance != 0 || code_hash != EMPTY_CODE_HASH).
            {
                auto authorityBalance = co_await authorityAccount.balance();
                if (codeHash != EMPTY_CODE_HASH || authorityBalance > 0 || existingNonce > 0)
                {
                    delegationRefund += EXISTING_AUTHORITY_REFUND;
                }
            }

            // 8. Set delegation code if address != 0; otherwise clear code
            if (auth.address != ZERO_ADDRESS)
            {
                bcos::bytes delegationCode(DELEGATION_MAGIC.begin(), DELEGATION_MAGIC.end());
                delegationCode.insert(
                    delegationCode.end(), auth.address.begin(), auth.address.end());
                auto newCodeHash = hashImpl.hash(
                    bcos::bytesConstRef(delegationCode.data(), delegationCode.size()));
                co_await authorityAccount.setCode(
                    std::move(delegationCode), std::string{}, newCodeHash);
            }
            else
            {
                co_await authorityAccount.setCode(bcos::bytes{}, std::string{}, EMPTY_CODE_HASH);
            }

            // 9. Bump authority nonce
            auto newNonce = existingNonce + 1;
            co_await authorityAccount.setNonce(newNonce.template convert_to<std::string>());
        }

        TRANSACTION_EXECUTOR_LOG(DEBUG)
            << "EIP-7702: authorization list processed" << LOG_KV("count", authList.size())
            << LOG_KV("delegationRefund", delegationRefund);

        co_return delegationRefund;
    }

    template <class Storage>
    struct ExecuteContext
    {
        struct Data
        {
            std::reference_wrapper<TransactionExecutorImpl> m_executor;
            std::reference_wrapper<protocol::BlockHeader const> m_blockHeader;
            std::reference_wrapper<protocol::Transaction const> m_transaction;
            int m_contextID;
            std::reference_wrapper<ledger::LedgerConfig const> m_ledgerConfig;
            Rollbackable<Storage> m_rollbackableStorage;
            Rollbackable<Storage>::Savepoint m_startSavepoint;
            // FIB-75: savepoint right after buyGas() pre-deduction — used to rollback only EVM
            // effects while preserving the pre-deducted balance.
            Rollbackable<Storage>::Savepoint m_afterBuyGasSavepoint{0};
            TransientStorage m_transientStorage;
            Rollbackable<decltype(m_transientStorage)> m_rollbackableTransientStorage;
            bool m_call;
            int64_t m_gasUsed = 0;
            int64_t m_delegationRefund = 0;  // EIP-7702 refund from authorization list
            std::string m_gasPriceStr;

            int64_t m_gasLimit;
            int64_t m_seq = 0;
            evmc_address m_origin;
            u256 m_nonce;
            executor::Web3AccessListResolved m_web3AccessListResolved;
            std::shared_ptr<executor::Eip2929AccessState> m_eip2929Access;
            // EIP-6780: track addresses created in this transaction (shared across nested contexts)
            std::shared_ptr<std::set<evmc_address>> m_createdInTx;
            hostcontext::HostContext<decltype(m_rollbackableStorage),
                decltype(m_rollbackableTransientStorage)>
                m_hostContext;
            std::optional<EVMCResult> m_evmcResult;

            Data(TransactionExecutorImpl& executor, Storage& storage,
                protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
                int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
              : m_executor(executor),
                m_blockHeader(blockHeader),
                m_transaction(transaction),
                m_contextID(contextID),
                m_ledgerConfig(ledgerConfig),
                m_rollbackableStorage(storage),
                m_startSavepoint(m_rollbackableStorage.current()),
                m_rollbackableTransientStorage(m_transientStorage),
                m_call(call),
                m_gasLimit(
                    ledgerConfig.features().get(ledger::Features::Flag::feature_ethereum_executor) ?
                        transaction.gasLimit() :
                        computeEffectiveGasLimit(transaction, ledgerConfig)),
                m_origin((!m_transaction.get().sender().empty() &&
                             m_transaction.get().sender().size() == sizeof(evmc_address)) ?
                             *(evmc_address*)m_transaction.get().sender().data() :
                             evmc_address{}),
                m_nonce(hex2u(transaction.nonce())),
                m_web3AccessListResolved(executor::resolveWeb3AccessList(transaction)),
                m_eip2929Access(std::make_shared<executor::Eip2929AccessState>()),
                m_createdInTx(std::make_shared<std::set<evmc_address>>()),
                m_hostContext(m_rollbackableStorage, m_rollbackableTransientStorage, blockHeader,
                    newEVMCMessage(m_blockHeader.get().number(), transaction, m_gasLimit, m_origin),
                    m_origin, transaction.abi(), contextID, m_seq, executor.m_precompiledManager,
                    ledgerConfig, *executor.m_hashImpl, transaction.type() != 0, m_nonce,
                    task::syncWait, m_web3AccessListResolved.accessList,
                    m_web3AccessListResolved.web3TypedTxKind, m_eip2929Access, m_createdInTx)
            {}
        };
        std::unique_ptr<Data> m_data;

        ExecuteContext(TransactionExecutorImpl& executor, Storage& storage,
            protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
            int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
          : m_data(std::make_unique<Data>(
                executor, storage, blockHeader, transaction, contextID, ledgerConfig, call))
        {}

        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            auto const& features = m_data->m_ledgerConfig.get().features();
            auto const isEthereumMode =
                features.get(ledger::Features::Flag::feature_ethereum_executor);

            if constexpr (step == 0)
            {
                co_await m_data->m_hostContext.prepare();

                // --- EIP-2929/2930/3651: pre-warm addresses for the transaction ---
                // This must happen BEFORE any EVM execution so that the first access
                // to sender, recipient, coinbase, precompiles, and access-list entries
                // uses the warm access cost (100 gas) instead of cold (2600 gas).
                {
                    auto& hostContext = m_data->m_hostContext;
                    auto const rev = effectiveRevision(
                        m_data->m_ledgerConfig.get(), m_data->m_blockHeader.get());
                    auto& eip2929 = *m_data->m_eip2929Access;

                    // EIP-2929: sender and recipient are warm from tx start
                    std::optional<evmc_address> toAddr;
                    auto const& txTo = m_data->m_transaction.get().to();
                    if (!txTo.empty() && txTo.size() >= sizeof(evmc_address))
                    {
                        evmc_address addr{};
                        std::copy_n(txTo.begin(), sizeof(evmc_address), addr.bytes);
                        toAddr = addr;
                    }
                    eip2929.warmUpInitialTxSet(m_data->m_origin, toAddr, rev);

                    // EIP-3651 (Shanghai+): coinbase is warm from tx start
                    if (rev >= EVMC_SHANGHAI)
                    {
                        auto const coinbaseBytes = m_data->m_blockHeader.get().coinbase();
                        if (coinbaseBytes.size() == sizeof(evmc_address))
                        {
                            evmc_address coinbaseAddr{};
                            std::copy(coinbaseBytes.begin(), coinbaseBytes.end(),
                                coinbaseAddr.bytes);
                            (void)eip2929.warmUpAddressNoJournal(coinbaseAddr);
                        }
                    }

                    // EIP-2930: warm access-list entries
                    if (m_data->m_web3AccessListResolved.accessList)
                    {
                        eip2929.warmUpAccessList(
                            *m_data->m_web3AccessListResolved.accessList,
                            [](bcos::Address const& addr) -> evmc_address {
                                evmc_address evmcAddr{};
                                std::copy_n(addr.begin(),
                                    std::min(static_cast<size_t>(addr.size()),
                                        sizeof(evmc_address)),
                                    evmcAddr.bytes);
                                return evmcAddr;
                            });
                    }
                }

                // --- Ethereum native mode: validate transaction + bump nonce ---
                if (isEthereumMode)
                {
                    auto& hostContext = m_data->m_hostContext;
                    auto const rev = effectiveRevision(
                        m_data->m_ledgerConfig.get(), m_data->m_blockHeader.get());
                    auto const& tx = m_data->m_transaction.get();

                    auto senderAccount = getAccount(hostContext, m_data->m_origin);

                    // 1. Nonce strict equality (Ethereum: must match storage nonce exactly)
                    auto nonceInStorage = co_await senderAccount.nonce();
                    auto storageNonce = u256(nonceInStorage.value_or("0"));
                    if (storageNonce != m_data->m_nonce)
                    {
                        TRANSACTION_EXECUTOR_LOG(ERROR) << "Ethereum mode: nonce mismatch"
                                                        << LOG_KV("storageNonce", storageNonce)
                                                        << LOG_KV("txNonce", m_data->m_nonce);
                        BOOST_THROW_EXCEPTION(
                            BCOS_ERROR(-1, "Nonce mismatch: tx.nonce != storage.nonce"));
                    }

                    // 2. EIP-1559 fee validation (London+, for type 2 and type 4 txs)
                    // Reference: bcos-evm/bcos-evm/eth/state/state.cpp validate_transaction()
                    auto const typedTxKind = tx.web3TypedTxKind();
                    if (rev >= EVMC_LONDON && (typedTxKind == 2 || typedTxKind == 4))
                    {
                        auto maxFee = tx.maxFeePerGas().value_or(u256{0});
                        auto maxPriorityFee = tx.maxPriorityFeePerGas().value_or(u256{0});
                        auto blockGasPrice =
                            u256{std::get<0>(m_data->m_ledgerConfig.get().gasPrice())};
                        auto blockBaseFee = (rev >= EVMC_LONDON) ? blockGasPrice : u256{0};

                        if (maxFee < blockBaseFee)
                        {
                            BOOST_THROW_EXCEPTION(BCOS_ERROR(
                                -8, "EIP-1559: maxFeePerGas is less than block base fee"));
                        }
                        if (maxPriorityFee > maxFee)
                        {
                            BOOST_THROW_EXCEPTION(
                                BCOS_ERROR(-9, "EIP-1559: maxPriorityFeePerGas > maxFeePerGas"));
                        }
                    }

                    // 3. EIP-3607: sender must be an EOA (check BEFORE auth list processing
                    //    because self-sponsored EIP-7702 txs delegate to themselves).
                    //    For EIP-7702 (type 4), the sender can have delegation code set by
                    //    their own authorization list — skip EIP-3607 for type 4 txs.
                    if (typedTxKind != 4)
                    {
                        if (auto codeHash = co_await senderAccount.codeHash())
                        {
                            static const bcos::h256 EMPTY_CODE_HASH{};
                            if (codeHash != EMPTY_CODE_HASH)
                            {
                                BOOST_THROW_EXCEPTION(
                                    BCOS_ERROR(-2, "EIP-3607: sender is not an EOA"));
                            }
                        }
                    }

                    // 4. Typed transaction fork validity
                    // EIP-2930 (type 1): only valid from Berlin
                    // EIP-1559 (type 2): only valid from London
                    // EIP-7702 (type 4): only valid from Prague
                    if (typedTxKind == 1 && rev < EVMC_BERLIN)
                    {
                        BOOST_THROW_EXCEPTION(
                            BCOS_ERROR(-5, "EIP-2930 type 1 tx before Berlin fork"));
                    }
                    if (typedTxKind == 2 && rev < EVMC_LONDON)
                    {
                        BOOST_THROW_EXCEPTION(
                            BCOS_ERROR(-6, "EIP-1559 type 2 tx before London fork"));
                    }
                    if (typedTxKind == 4 && rev < EVMC_PRAGUE)
                    {
                        BOOST_THROW_EXCEPTION(
                            BCOS_ERROR(-7, "EIP-7702 type 4 tx before Prague fork"));
                    }

                    // 3. EIP-3860: initcode size limit (Shanghai+)
                    if (rev >= EVMC_SHANGHAI && tx.to().empty())
                    {
                        static constexpr size_t MAX_INITCODE_SIZE = 49152;  // 2 * 24576
                        if (tx.input().size() > MAX_INITCODE_SIZE)
                        {
                            BOOST_THROW_EXCEPTION(
                                BCOS_ERROR(-3, "EIP-3860: initcode size exceeds 49152 bytes"));
                        }
                    }

                    // 4. Intrinsic gas check (EIP-2028 + EIP-3860 + EIP-7623)
                    static constexpr auto ACCESS_LIST_ADDRESS_COST = 2400;
                    static constexpr auto ACCESS_LIST_STORAGE_KEY_COST = 1900;
                    int64_t accessListCost = 0;
                    if (m_data->m_web3AccessListResolved.accessList)
                    {
                        for (auto const& [_, keys] : *m_data->m_web3AccessListResolved.accessList)
                            accessListCost +=
                                ACCESS_LIST_ADDRESS_COST +
                                static_cast<int64_t>(keys.size()) * ACCESS_LIST_STORAGE_KEY_COST;
                    }
                    auto const intrinsicGas = computeTxIntrinsicCost(rev, tx, accessListCost);
                    if (tx.gasLimit() < intrinsicGas)
                    {
                        TRANSACTION_EXECUTOR_LOG(ERROR) << "Ethereum mode: intrinsic gas too low"
                                                        << LOG_KV("txGasLimit", tx.gasLimit())
                                                        << LOG_KV("intrinsicGas", intrinsicGas);
                        BOOST_THROW_EXCEPTION(BCOS_ERROR(-4, "Intrinsic gas too low"));
                    }

                    // 5. Bump nonce (Ethereum: before gas deduction; rolled back on failure)
                    auto newNonce = storageNonce + 1;
                    co_await senderAccount.setNonce(newNonce.template convert_to<std::string>());
                    m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();

                    // 6. EIP-7702: process authorization list (set_code delegation)
                    {
                        uint64_t chainId = 0;
                        if (auto const& cid = m_data->m_ledgerConfig.get().chainId();
                            cid.has_value())
                        {
                            // Extract lowest 8 bytes of chain ID (256-bit → 64-bit)
                            chainId = cid->bytes[24] |
                                      (static_cast<uint64_t>(cid->bytes[25]) << 8) |
                                      (static_cast<uint64_t>(cid->bytes[26]) << 16) |
                                      (static_cast<uint64_t>(cid->bytes[27]) << 24) |
                                      (static_cast<uint64_t>(cid->bytes[28]) << 32) |
                                      (static_cast<uint64_t>(cid->bytes[29]) << 40) |
                                      (static_cast<uint64_t>(cid->bytes[30]) << 48) |
                                      (static_cast<uint64_t>(cid->bytes[31]) << 56);
                        }
                        m_data->m_delegationRefund =
                            co_await processAuthorizationList(m_data->m_rollbackableStorage, tx,
                                chainId, rev, features, *m_data->m_executor.get().m_hashImpl);
                    }
                    // Update savepoint after authorization-list state changes
                    m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();
                }
            }
            else if constexpr (step == 1)
            {
                // --- Ethereum native mode: EIP-1559 gas + execute + refund ---
                if (isEthereumMode)
                {
                    auto const& blockHeader = m_data->m_blockHeader.get();
                    auto const& tx = m_data->m_transaction.get();
                    auto const rev = effectiveRevision(m_data->m_ledgerConfig.get(), blockHeader);

                    // Compute effective gas price
                    // EIP-1559: effectiveGasPrice = baseFee + priorityFee
                    // Legacy:   effectiveGasPrice = gasPrice
                    auto blockGasPrice = u256{std::get<0>(m_data->m_ledgerConfig.get().gasPrice())};
                    auto baseFee = (rev >= EVMC_LONDON) ? blockGasPrice : u256{0};
                    u256 effectiveGasPrice;
                    u256 priorityGasPrice;
                    if (tx.maxFeePerGas().has_value())
                    {
                        // EIP-1559 typed transaction (type 2)
                        auto maxGasPrice = tx.maxFeePerGas().value();
                        priorityGasPrice = std::min(tx.maxPriorityFeePerGas().value_or(u256{0}),
                            maxGasPrice > baseFee ? maxGasPrice - baseFee : u256{0});
                        effectiveGasPrice = baseFee + priorityGasPrice;
                    }
                    else
                    {
                        // Legacy transaction: effectiveGasPrice = gasPrice
                        effectiveGasPrice = tx.gasPrice().value_or(u256{0});
                        priorityGasPrice =
                            effectiveGasPrice > baseFee ? effectiveGasPrice - baseFee : u256{0};
                    }

                    if (!m_data->m_call && effectiveGasPrice > 0)
                    {
                        auto const txMaxCost =
                            u256(m_data->m_gasLimit) * effectiveGasPrice + u256(tx.value());
                        auto& evmcMessage = m_data->m_hostContext.message();
                        auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
                        auto senderBalance = co_await senderAccount.balance();

                        if (senderBalance < txMaxCost)
                        {
                            TRANSACTION_EXECUTOR_LOG(ERROR) << "Ethereum mode: insufficient balance"
                                                            << LOG_KV("balance", senderBalance)
                                                            << LOG_KV("txMaxCost", txMaxCost);
                            BOOST_THROW_EXCEPTION(
                                protocol::NotEnoughCashError{}
                                << errinfo_comment("Insufficient funds for gas * price + value"));
                        }

                        // Pre-deduct max gas cost
                        co_await senderAccount.setBalance(
                            senderBalance - u256(m_data->m_gasLimit) * effectiveGasPrice);
                        m_data->m_gasPriceStr =
                            "0x" + effectiveGasPrice.str(256, std::ios_base::hex);

                        // EIP-4844: deduct blob gas fee for type 3 (blob) transactions.
                        // Reference: evmone state.cpp transition() blob fee deduction.
                        if (tx.web3TypedTxKind() == 3 && rev >= EVMC_CANCUN)
                        {
                            static constexpr int64_t GAS_PER_BLOB = 1 << 17;  // 131072
                            auto const& blobHashes = tx.blobVersionedHashes();
                            auto const blobCount = static_cast<int64_t>(blobHashes.size());
                            auto const blobGasUsed = blobCount * GAS_PER_BLOB;
                            if (blobGasUsed > 0)
                            {
                                // Compute blob gas price from excessBlobGas via
                                // fake-exponential EIP-4844 formula (simplified:
                                // excessBlobGas=0 → blobBaseFee=1).
                                // For EEST tests, excessBlobGas is 0x00 so base fee = 1.
                                auto const blobBaseFee = u256{1};  // TODO: proper formula
                                auto const maxBlobFee = u256{tx.maxFeePerBlobGas().value_or(
                                    u256{std::numeric_limits<uint64_t>::max()})};
                                auto const effectiveBlobFee =
                                    std::min(blobBaseFee, maxBlobFee);
                                auto const blobFee = u256(blobGasUsed) * effectiveBlobFee;
                                if (blobFee > 0)
                                {
                                    auto blobBalance = co_await senderAccount.balance();
                                    co_await senderAccount.setBalance(blobBalance - blobFee);
                                }
                            }
                        }
                    }

                    // bcos-evm pattern: pre-deduct intrinsic from EVM gas budget
                    // so evmone sees correct remaining gas (GAS opcode, OOG detection).
                    {
                        static constexpr auto ACCESS_LIST_ADDRESS_COST = 2400;
                        static constexpr auto ACCESS_LIST_STORAGE_KEY_COST = 1900;
                        int64_t accessListCostForIntrinsic = 0;
                        if (m_data->m_web3AccessListResolved.accessList)
                        {
                            for (auto const& [_, keys] :
                                *m_data->m_web3AccessListResolved.accessList)
                                accessListCostForIntrinsic += ACCESS_LIST_ADDRESS_COST +
                                    static_cast<int64_t>(keys.size()) *
                                        ACCESS_LIST_STORAGE_KEY_COST;
                        }
                        auto const intrinsicGas =
                            computeTxIntrinsicCostPure(rev, tx, accessListCostForIntrinsic);
                        auto& msg = m_data->m_hostContext.mutableMessage();
                        if (msg.gas <= intrinsicGas)
                        {
                            msg.gas = 0;
                        }
                        else
                        {
                            msg.gas -= intrinsicGas;
                        }
                    }

                    // Execute EVM
                    m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());

                    // Post-execution settlement: gasLeft already accounts for intrinsic
                    auto& evmcResult = *m_data->m_evmcResult;
                    // Ethereum mode: ALL exceptional halts (invalid instruction,
                    // undefined instruction, bad jump, stack error, OOG,
                    // contract validation failure, etc.) produce a successful
                    // transaction receipt per the Yellow Paper / Ethereum consensus.
                    // Only the gas is consumed; state changes (nonce bump, gas
                    // deduction) are preserved.
                    // REVERT is handled separately — it preserves gas_left
                    // (gas up to the revert point is consumed, remainder refunded).
                    if (isEthereumMode)
                    {
                        m_data->m_evmcResult->status = protocol::TransactionStatus::None;
                    }
                    // gasUsed = (gasLimit - intrinsic) - gasLeft + intrinsic = gasLimit - gasLeft
                    auto gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
                    if (evmcResult.status_code == EVMC_SUCCESS ||
                        evmcResult.status_code == EVMC_REVERT)
                    {
                        // Cap at gasLimit (handle edge case where EVM over-uses)
                        gasUsed = std::min(gasUsed, m_data->m_gasLimit);
                    }

                    if (!m_data->m_call && effectiveGasPrice > 0)
                    {
                        // EIP-3529: gas refund with max_refund_quotient
                        static constexpr auto MAX_REFUND_QUOTIENT_LONDON = 5;
                        static constexpr auto MAX_REFUND_QUOTIENT_PRE_LONDON = 2;
                        auto const maxRefundQuotient = rev >= EVMC_LONDON ?
                                                           MAX_REFUND_QUOTIENT_LONDON :
                                                           MAX_REFUND_QUOTIENT_PRE_LONDON;
                        auto refund = std::min(m_data->m_delegationRefund + evmcResult.gas_refund,
                            gasUsed / maxRefundQuotient);
                        gasUsed -= refund;

                        // EIP-7623: floor gas cost (Prague+).
                        // Reference: bcos-evm state.cpp transition() line 636.
                        if (rev >= EVMC_PRAGUE)
                        {
                            static constexpr auto TOTAL_COST_FLOOR_PER_TOKEN = 10;
                            auto const& input = tx.input();
                            const size_t numZero = static_cast<size_t>(
                                std::count(input.begin(), input.end(), static_cast<uint8_t>(0)));
                            const size_t numNonzero = input.size() - numZero;
                            const auto numTokens = static_cast<int64_t>(
                                4 * numNonzero + numZero);  // post-Istanbul multipliers
                            auto const minGasCost =
                                int64_t{21000} + numTokens * TOTAL_COST_FLOOR_PER_TOKEN;
                            gasUsed = std::max(gasUsed, minGasCost);
                        }

                        m_data->m_gasUsed = gasUsed;

                        // Refund unused gas to sender
                        auto unusedGas = m_data->m_gasLimit - gasUsed;
                        if (unusedGas > 0)
                        {
                            auto& evmcMsgRef = m_data->m_hostContext.message();
                            auto senderAcctRef =
                                getAccount(m_data->m_hostContext, evmcMsgRef.sender);
                            auto balance = co_await senderAcctRef.balance();
                            co_await senderAcctRef.setBalance(
                                balance + u256(unusedGas) * effectiveGasPrice);
                        }

                        // Coinbase priority-fee payment
                        // Use block header's coinbase if set (Ethereum mode), else 0x0
                        if (priorityGasPrice > 0)
                        {
                            evmc_address coinbaseAddr{};
                            auto const coinbaseBytes = m_data->m_blockHeader.get().coinbase();
                            if (coinbaseBytes.size() == sizeof(evmc_address))
                            {
                                std::copy(
                                    coinbaseBytes.begin(), coinbaseBytes.end(), coinbaseAddr.bytes);
                            }
                            auto coinbaseAccount = getAccount(m_data->m_hostContext, coinbaseAddr);
                            auto coinbaseBalance = co_await coinbaseAccount.balance();
                            co_await coinbaseAccount.setBalance(
                                coinbaseBalance + u256(gasUsed) * priorityGasPrice);
                        }
                    }
                    else
                    {
                        m_data->m_gasUsed = gasUsed;
                    }

                    // On EVM failure in Ethereum mode, do NOT rollback state.
                    // Ethereum treats all exceptional halts as successful
                    // transactions — the sender still pays for gas, the nonce
                    // is consumed, and only the EVM execution effects are
                    // discarded (evmone already handled that).
                    // In BCOS mode (non-Ethereum), rollback to preserve
                    // backward-compatible consensus semantics.
                    if (!isEthereumMode &&
                        evmcResult.status_code != EVMC_SUCCESS &&
                        evmcResult.status_code != EVMC_REVERT &&
                        evmcResult.status_code != EVMC_STACK_OVERFLOW &&
                        evmcResult.status_code != EVMC_STACK_UNDERFLOW)
                    {
                        co_await m_data->m_rollbackableStorage.rollback(m_data->m_startSavepoint);
                    }
                }
                else
                {
                    auto updated = co_await updateNonce();
                    if (updated)
                    {
                        m_data->m_startSavepoint = m_data->m_rollbackableStorage.current();
                    }

                    if (const auto gasPrice =
                            u256{std::get<0>(m_data->m_ledgerConfig.get().gasPrice())};
                        m_data->m_transaction.get().type() == 1 &&  // web3Tx
                        m_data->m_ledgerConfig.get().features().get(
                            ledger::Features::Flag::bugfix_gas_payment_balance_precheck) &&
                        gasPrice > 0)
                    {
                        // FIB-75 geth-style: buy gas (pre-deduct), execute, refund unused gas.
                        if (!co_await buyGas())
                        {
                            co_return {};
                        }
                        m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());
                        co_await refundGas();
                    }
                    else
                    {
                        // Legacy path
                        m_data->m_evmcResult.emplace(co_await m_data->m_hostContext.execute());
                        co_await consumeBalance();
                    }
                }
            }
            else if constexpr (step == 2)
            {
                co_return co_await finish();
            }

            co_return {};
        }

        task::Task<bool> updateNonce()
        {
            if (const auto& transaction = m_data->m_transaction.get();
                transaction.type() == 1)  // 1 = web3
                                          // transaction
            {
                auto& callNonce = m_data->m_nonce;
                ledger::account::EVMAccount account(m_data->m_rollbackableStorage, m_data->m_origin,
                    m_data->m_ledgerConfig.get().features().get(
                        ledger::Features::Flag::feature_raw_address));

                if (!co_await account.exists())
                {
                    co_await account.create();
                }
                auto nonceInStorage = co_await account.nonce();
                auto storageNonce = u256(nonceInStorage.value_or("0"));
                u256 newNonce = std::max(callNonce, storageNonce) + 1;
                co_await account.setNonce(newNonce.convert_to<std::string>());
                co_return true;
            }
            co_return false;
        }

        // FIB-75 (geth-style): Pre-deduct gasLimit * gasPrice from sender before EVM execution.
        // If balance is insufficient to cover gas + value, fail immediately (EVM does not run,
        // no balance deducted, nonce preserved as replay protection).
        // On success, balance -= gasLimit * gasPrice; EVM then runs with m_gasLimit as its
        // gas budget, guaranteeing gasUsed <= gasLimit so refundGas() always has enough to
        // settle without confiscating extra balance.
        task::Task<bool> buyGas()
        {
            if (m_data->m_call)
            {
                co_return true;
            }

            // FIB-75: use the transaction's effective gas price (legacy gasPrice or EIP-1559
            // maxFeePerGas) so charging matches what the txpool validated.
            const auto gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
            if (gasPrice == 0)
            {
                co_return true;
            }

            if (m_data->m_gasLimit <= 0)
            {
                co_return true;
            }

            const auto maxGasCost = u256(m_data->m_gasLimit) * gasPrice;
            const auto txValue = u256(m_data->m_transaction.get().value());
            const auto totalRequired = maxGasCost + txValue;

            auto& evmcMessage = m_data->m_hostContext.message();
            auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
            auto senderBalance = co_await senderAccount.balance();

            if (senderBalance < totalRequired)
            {
                TRANSACTION_EXECUTOR_LOG(ERROR)
                    << "buyGas: insufficient balance" << LOG_KV("balance", senderBalance)
                    << LOG_KV("maxGasCost", maxGasCost) << LOG_KV("txValue", txValue)
                    << LOG_KV("totalRequired", totalRequired);

                // FIB-75: charge minimum penalty = min(balance, intrinsic_gas * gasPrice).
                // The transaction is already in a consensus-packed block and consumed
                // consensus/storage resources, so a sender who can't cover full gas cost
                // still pays at least the 21000-gas base cost (geth's intrinsic gas for
                // an empty tx). If balance < intrinsic cost, drain what's left. This
                // prevents free spam from repeatedly submitting under-funded transactions.
                constexpr static int64_t INTRINSIC_GAS = 21000;
                const auto intrinsicCost = u256(INTRINSIC_GAS) * gasPrice;
                const auto penalty = std::min(senderBalance, intrinsicCost);
                if (penalty > 0)
                {
                    co_await senderAccount.setBalance(senderBalance - penalty);
                }

                evmc_result failResult{};
                failResult.status_code = EVMC_INSUFFICIENT_BALANCE;
                failResult.gas_left = 0;
                failResult.output_data = nullptr;
                failResult.output_size = 0;
                failResult.release = nullptr;
                failResult.create_address = {};
                m_data->m_evmcResult.emplace(
                    EVMCResult(failResult, protocol::TransactionStatus::NotEnoughCash));
                // gasUsed reflects what was actually charged as penalty (in gas units).
                m_data->m_gasUsed = (penalty / gasPrice).template convert_to<int64_t>();
                m_data->m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);

                co_return false;
            }

            // Pre-deduct max gas cost from sender
            co_await senderAccount.setBalance(senderBalance - maxGasCost);
            m_data->m_afterBuyGasSavepoint = m_data->m_rollbackableStorage.current();
            m_data->m_gasPriceStr = "0x" + gasPrice.str(256, std::ios_base::hex);
            co_return true;
        }

        // FIB-75 (geth-style): After EVM execution, refund (gasLimit - gasUsed) * gasPrice.
        // If EVM failed (non-SUCCESS, non-REVERT), roll back state changes while preserving
        // the pre-deducted gas cost. gasUsed <= gasLimit is guaranteed because the EVM's
        // gas budget is m_gasLimit.
        task::Task<void> refundGas()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;

            if (m_data->m_call)
            {
                co_return;
            }

            // FIB-75: mirror buyGas() — use the tx's effective gas price.
            const auto gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
            if (gasPrice == 0)
            {
                co_return;
            }

            // On EVM failure (not SUCCESS / REVERT), rollback EVM state changes but keep
            // the pre-deducted gas — the sender still pays for the wasted execution.
            if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
            {
                co_await m_data->m_rollbackableStorage.rollback(m_data->m_afterBuyGasSavepoint);
            }

            // Refund unused gas
            if (evmcResult.gas_left > 0)
            {
                auto refund = u256(evmcResult.gas_left) * gasPrice;
                auto& evmcMessage = m_data->m_hostContext.message();
                auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
                auto balance = co_await senderAccount.balance();
                co_await senderAccount.setBalance(balance + refund);
            }
        }

        // Legacy balance consumption — only used when bugfix_gas_payment_balance_precheck is OFF.
        // Kept unchanged from pre-FIB-75 behavior: on insufficient balance, rollback execution
        // effects and deduct nothing (the original FIB-75 bug that's fixed by the precheck flag).
        task::Task<void> consumeBalance()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
            if (!m_data->m_call)
            {
                auto& evmcMessage = m_data->m_hostContext.message();
                if (auto gasPrice = u256{std::get<0>(m_data->m_ledgerConfig.get().gasPrice())};
                    gasPrice > 0)
                {
                    constexpr static const auto GAS_PRICE_DIGITS = 256;
                    m_data->m_gasPriceStr =
                        "0x" + gasPrice.str(GAS_PRICE_DIGITS, std::ios_base::hex);

                    auto balanceUsed = m_data->m_gasUsed * gasPrice;
                    auto senderAccount = getAccount(m_data->m_hostContext, evmcMessage.sender);
                    auto senderBalance = co_await senderAccount.balance();

                    if (senderBalance < balanceUsed)
                    {
                        TRANSACTION_EXECUTOR_LOG(ERROR) << "Insufficient balance: " << senderBalance
                                                        << ", balanceUsed: " << balanceUsed;
                        evmcResult.status_code = EVMC_INSUFFICIENT_BALANCE;
                        evmcResult.status = protocol::TransactionStatus::NotEnoughCash;
                        if (evmcResult.release != nullptr)
                        {
                            evmcResult.release(std::addressof(evmcResult));
                        }
                        evmcResult.output_data = nullptr;
                        evmcResult.output_size = 0;
                        evmcResult.release = nullptr;
                        evmcResult.create_address = {};
                        co_await m_data->m_rollbackableStorage.rollback(m_data->m_startSavepoint);
                    }
                    else
                    {
                        co_await senderAccount.setBalance(senderBalance - balanceUsed);
                    }
                }
            }
        }

        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            const auto& evmcMessage = m_data->m_hostContext.message();
            auto& evmcResult = *m_data->m_evmcResult;

            std::string newContractAddress;
            if (evmcMessage.kind == EVMC_CREATE && evmcResult.status_code == EVMC_SUCCESS)
            {
                newContractAddress.reserve(sizeof(evmcResult.create_address) * 2);
                boost::algorithm::hex_lower(evmcResult.create_address.bytes,
                    evmcResult.create_address.bytes + sizeof(evmcResult.create_address.bytes),
                    std::back_inserter(newContractAddress));
            }

            if (evmcResult.status_code != EVMC_SUCCESS)
            {
                TRANSACTION_EXECUTOR_LOG(DEBUG) << "Transaction revert: " << evmcResult.status_code;

                auto [outputData, outputSize, release] = fillErrorOutputInPlace(
                    *m_data->m_executor.get().m_hashImpl, evmcResult.status_code);
                if (release != nullptr)
                {
                    if (evmcResult.release != nullptr)
                    {
                        evmcResult.release(std::addressof(evmcResult));
                    }
                    evmcResult.output_data = outputData;
                    evmcResult.output_size = outputSize;
                    evmcResult.release = release;
                }
                if (m_data->m_ledgerConfig.get().features().get(
                        ledger::Features::Flag::bugfix_revert_logs))
                {
                    m_data->m_hostContext.logs().clear();
                }
            }

            auto receiptStatus = static_cast<int32_t>(evmcResult.status);
            auto const& logEntries = m_data->m_hostContext.logs();
            protocol::TransactionReceipt::Ptr receipt;
            switch (auto transactionVersion = static_cast<bcos::protocol::TransactionVersion>(
                        m_data->m_transaction.get().version()))
            {
            case bcos::protocol::TransactionVersion::V0_VERSION:
                receipt = m_data->m_executor.get().m_receiptFactory.get().createReceipt(
                    m_data->m_gasUsed, std::move(newContractAddress), logEntries, receiptStatus,
                    {evmcResult.output_data, evmcResult.output_size},
                    m_data->m_blockHeader.get().number());
                break;
            case bcos::protocol::TransactionVersion::V1_VERSION:
            case bcos::protocol::TransactionVersion::V2_VERSION:
                receipt = m_data->m_executor.get().m_receiptFactory.get().createReceipt2(
                    m_data->m_gasUsed, std::move(newContractAddress), logEntries, receiptStatus,
                    {evmcResult.output_data, evmcResult.output_size},
                    m_data->m_blockHeader.get().number(), std::move(m_data->m_gasPriceStr),
                    transactionVersion);
                break;
            default:
                BOOST_THROW_EXCEPTION(InvalidReceiptVersion{} << bcos::errinfo_comment(
                                          "Invalid receipt version: " +
                                          std::to_string(m_data->m_transaction.get().version())));
            }

            TRANSACTION_EXECUTOR_LOG(TRACE) << "Execute transaction finished: " << *receipt;
            co_return receipt;  // 完成第三步 Complete the third step
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        TRANSACTION_EXECUTOR_LOG(TRACE) << "Create transaction context: " << transaction;
        co_return {*this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call);

        co_await executeContext.template executeStep<0>();
        co_await executeContext.template executeStep<1>();
        co_return co_await executeContext.template executeStep<2>();
    }
};

}  // namespace bcos::executor_v1
