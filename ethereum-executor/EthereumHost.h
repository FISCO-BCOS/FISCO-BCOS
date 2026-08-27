/// @file EthereumHost.h
/// @brief Ported evmone Host (eth/state/host.{hpp,cpp}) adapted to operate on
///        EthereumState over BCOS storage, using protocol::Transaction and
///        BlockHeader-derived block info directly — no evmone::state::BlockHashes
///        virtual interface (block-hash lookups are injected as a std::function)
///        and no evmone::state::Transaction conversion.
///
/// Consensus-critical logic is ported verbatim from bcos-evm (itself a vendored
/// evmone): EIP-161 emptiness, EIP-2200/3529 storage refunds, EIP-2929/2930
/// access tracking, EIP-1153 transient storage, EIP-6780 SELFDESTRUCT semantics,
/// EIP-7702 delegation resolution, CREATE/CREATE2 address derivation, precompiles.

#pragma once

#include "EVMPrecompiles.h"
#include "EVMSupport.h"
#include "EthereumState.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-utilities/BoostLog.h"
#include <evmc/evmc.hpp>
#include <evmone/constants.hpp>
#include <evmone/delegation.hpp>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace bcos::executor_v1::eth
{
using namespace evmc::literals;

/// Block-level parameters the EVM needs, derived directly from the BCOS
/// protocol::BlockHeader + ledger::LedgerConfig by the executor. This is the
/// ported evmone::state::BlockInfo, trimmed to what the host/transition touch.
struct EthBlockInfo
{
    int64_t number = 0;
    int64_t timestamp = 0;
    int64_t gas_limit = 0;
    address coinbase;
    int64_t difficulty = 0;
    bytes32 prev_randao;
    /// The EIP-1559 base fee. Zero before London — consumers gate on the
    /// revision (get_tx_context here and split-4/4's fee accounting), so the
    /// caller may pass the raw configured value.
    uint64_t base_fee = 0;
    /// Blob gas used / excess (EIP-4844).
    std::optional<uint64_t> blob_gas_used;
    std::optional<uint64_t> excess_blob_gas;
    /// Blob gas price computed from excess_blob_gas (EIP-4844).
    std::optional<uint256> blob_base_fee;
};

/// Overrides applied only for the eth_call / eth_estimateGas dry-run path
/// (call == true), mirroring the RPC normalization the old executor performed on
/// its evmone tx copy. For real (call == false) execution every field is
/// std::nullopt / false and the values are read straight off the Transaction.
struct EthCallParams
{
    /// Effective gas limit; 0 tx gas -> block gas limit (capped to
    /// MAX_TX_GAS_LIMIT on Osaka+ per EIP-7825).
    std::optional<int64_t> gasLimit;
    /// Effective nonce; empty tx nonce -> the sender's current storage nonce.
    std::optional<uint64_t> nonce;
    /// Dry-runs are never charged: clear gas price / tip (and the block base fee
    /// is zeroed by the executor), closing the fee-cap and balance checks.
    bool free = false;
};

/// The sender of a bcos Transaction (raw 20 bytes).
inline address ethSender(protocol::Transaction const& tx)
{
    address a{};
    auto const& sb = tx.sender();
    if (sb.size() >= sizeof(evmc_address))
        std::copy_n(sb.begin(), sizeof(evmc_address), a.bytes);
    return a;
}

/// Effective max gas price (EIP-1559 maxFeePerGas, legacy gasPrice fallback).
inline uint256 ethMaxGasPrice(protocol::Transaction const& tx, EthCallParams const& callParams)
{
    if (callParams.free)
        return 0;
    if (auto mf = tx.maxFeePerGas(); mf.has_value())
        return *mf;
    if (auto gp = tx.gasPrice(); gp.has_value())
        return *gp;
    return 0;
}

/// Effective max priority gas price (EIP-1559).
inline uint256 ethMaxPriorityGasPrice(
    protocol::Transaction const& tx, EthCallParams const& callParams)
{
    if (callParams.free)
        return 0;
    // Legacy / access-list txs carry no priority-fee field in their signed RLP:
    // the whole gas price is the tip above the base fee. Decide on the kind
    // FIRST, so an unvalidated Tars mirror value (present-and-zero, reachable
    // over P2P) cannot override it — the old bridge's `== 0` fixup had the
    // same effect.
    if (tx.web3TypedTxKind() <= 1)
        return ethMaxGasPrice(tx, callParams);
    if (auto mp = tx.maxPriorityFeePerGas(); mp.has_value())
        return *mp;
    return 0;
}

/// Max blob gas price (EIP-4844).
inline uint256 ethMaxBlobGasPrice(protocol::Transaction const& tx)
{
    if (auto mb = tx.maxFeePerBlobGas(); mb.has_value())
        return *mb;
    return 0;
}

/// Effective gas limit, applying the eth_call normalization.
inline int64_t effectiveGasLimit(protocol::Transaction const& tx, EthCallParams const& callParams)
{
    if (callParams.gasLimit.has_value())
        return *callParams.gasLimit;
    return tx.gasLimit();
}

/// Effective nonce, applying the eth_call normalization.
inline uint64_t effectiveNonce(protocol::Transaction const& tx, EthCallParams const& callParams)
{
    if (callParams.nonce.has_value())
        return *callParams.nonce;
    return bcos::safeFromQuantity(tx.nonce()).value_or(0);
}

/// A block-hash lookup function (BLOCKHASH opcode). Replaces the virtual
/// evmone::state::BlockHashes interface: the executor injects a storage-backed
/// lambda (production) or an in-memory map (EEST), and the host guarantees it is
/// never invoked past a noexcept boundary without a fail-safe.
/// Resolves the hash of a past block for the BLOCKHASH opcode.
///
/// @param blockNumber    the height being queried (an ancestor of the executing
///                       block).
/// @param currentHeight  the height of the block currently being executed,
///                       supplied by the host (m_block.number — already in the
///                       execution context) so the provider can bound BLOCKHASH
///                       to the last 256 ancestors without an extra storage read
///                       for the current height.
using BlockHashLookup = std::function<evmc::bytes32(int64_t blockNumber, int64_t currentHeight)>;

/// Ported evmone::state::Host over EthereumState.
///
/// @tparam Storage the BCOS storage backend (raw or Rollbackable wrapper).
template <class Storage>
class EthereumHost : public evmc::Host
{
    evmc_revision m_rev;
    evmc::VM& m_vm;
    EthereumState<Storage>& m_state;
    EthBlockInfo const& m_block;
    BlockHashLookup m_blockHashLookup;
    protocol::Transaction const& m_tx;
    // By value, not by const-ref: the natural construction site for real
    // (call == false) execution passes EthCallParams{} as a temporary, and a
    // reference member would dangle past the full-expression. It is only two
    // optionals + a bool.
    EthCallParams m_callParams;
    std::vector<evm::Log> m_logs;
    // Stable copy of the tx blob hashes for get_tx_context (points into this).
    std::vector<bytes32> m_blobHashes;
    // The node's chain id (EIP-155), surfaced to the EVM via get_tx_context's
    // chain_id field. Used by the CHAINID opcode (e.g. EIP-712 domain
    // separators baked into contract runtime code), so it must be the real
    // chain's id, NOT a hard-coded 1.
    uint64_t m_chainId;

public:
    EthereumHost(evmc_revision rev, evmc::VM& vm, EthereumState<Storage>& state,
        EthBlockInfo const& block, BlockHashLookup blockHashLookup, protocol::Transaction const& tx,
        EthCallParams const& callParams, uint64_t chainId)
      : m_rev{rev},
        m_vm{vm},
        m_state{state},
        m_block{block},
        m_blockHashLookup{std::move(blockHashLookup)},
        m_tx{tx},
        m_callParams{callParams},
        m_chainId{chainId}
    {
        for (auto const& h : tx.blobVersionedHashes())
        {
            bytes32 hash{};
            std::copy_n(h.begin(), sizeof(evmc_bytes32), hash.bytes);
            m_blobHashes.push_back(hash);
        }
    }

    [[nodiscard]] std::vector<evm::Log>&& take_logs() noexcept { return std::move(m_logs); }

    evmc::Result call(const evmc_message& msg) noexcept override;

private:
    [[nodiscard]] bool account_exists(const address& addr) const noexcept override;

    [[nodiscard]] bytes32 get_storage(
        const address& addr, const bytes32& key) const noexcept override;

    evmc_storage_status set_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept override;

    [[nodiscard]] evmc::bytes32 get_transient_storage(
        const address& addr, const bytes32& key) const noexcept override;

    void set_transient_storage(
        const address& addr, const bytes32& key, const bytes32& value) noexcept override;

    [[nodiscard]] evmc::uint256be get_balance(const address& addr) const noexcept override;

    [[nodiscard]] size_t get_code_size(const address& addr) const noexcept override;

    [[nodiscard]] bytes32 get_code_hash(const address& addr) const noexcept override;

    size_t copy_code(const address& addr, size_t code_offset, uint8_t* buffer_data,
        size_t buffer_size) const noexcept override;

    bool selfdestruct(const address& addr, const address& beneficiary) noexcept override;

    evmc::Result create(const evmc_message& msg) noexcept;

    [[nodiscard]] evmc_tx_context get_tx_context() const noexcept override;

    [[nodiscard]] bytes32 get_block_hash(int64_t block_number) const noexcept override;

    void emit_log(const address& addr, const uint8_t* data, size_t data_size,
        const bytes32 topics[], size_t topics_count) noexcept override;

public:
    evmc_access_status access_account(const address& addr) noexcept override;

private:
    evmc_access_status access_storage(const address& addr, const bytes32& key) noexcept override;

    /// Prepares message for execution (ported host.cpp prepare_message).
    std::optional<evmc_message> prepare_message(evmc_message msg) noexcept;

    /// Executes a prepared message (ported host.cpp execute_message).
    evmc::Result execute_message(const evmc_message& msg) noexcept;
};

template <class Storage>
bool EthereumHost<Storage>::account_exists(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    return acc != nullptr && (m_rev < EVMC_SPURIOUS_DRAGON || !acc->is_empty());
}

template <class Storage>
bytes32 EthereumHost<Storage>::get_storage(const address& addr, const bytes32& key) const noexcept
{
    return m_state.get_storage(addr, key).current;
}

template <class Storage>
evmc_storage_status EthereumHost<Storage>::set_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    // Follow EVMC documentation https://evmc.ethereum.org/storagestatus.html#autotoc_md3
    // and EIP-2200 specification https://eips.ethereum.org/EIPS/eip-2200.
    auto& storage_slot = m_state.get_storage(addr, key);
    const auto& [current, original, _] = storage_slot;

    const auto dirty = original != current;
    const auto restored = original == value;
    const auto current_is_zero = is_zero(current);
    const auto value_is_zero = is_zero(value);

    auto status = EVMC_STORAGE_ASSIGNED;  // All other cases.
    if (!dirty && !restored)
    {
        if (current_is_zero)
            status = EVMC_STORAGE_ADDED;  // 0 → 0 → Z
        else if (value_is_zero)
            status = EVMC_STORAGE_DELETED;  // X → X → 0
        else
            status = EVMC_STORAGE_MODIFIED;  // X → X → Z
    }
    else if (dirty && !restored)
    {
        if (current_is_zero && !value_is_zero)
            status = EVMC_STORAGE_DELETED_ADDED;  // X → 0 → Z
        else if (!current_is_zero && value_is_zero)
            status = EVMC_STORAGE_MODIFIED_DELETED;  // X → Y → 0
    }
    else if (dirty)
    {
        assert(restored);  // Always true.
        if (current_is_zero)
            status = EVMC_STORAGE_DELETED_RESTORED;  // X → 0 → X
        else if (value_is_zero)
            status = EVMC_STORAGE_ADDED_DELETED;  // 0 → Y → 0
        else
            status = EVMC_STORAGE_MODIFIED_RESTORED;  // X → Y → X
    }

    // In Berlin this is handled in access_storage().
    if (m_rev < EVMC_BERLIN)
        m_state.journal_storage_change(addr, key, storage_slot);
    storage_slot.current = value;  // Update current value.
    return status;
}

template <class Storage>
bytes32 EthereumHost<Storage>::get_transient_storage(
    const address& addr, const bytes32& key) const noexcept
{
    const auto& acc = m_state.get(addr);
    const auto it = acc.transient_storage.find(key);
    return it != acc.transient_storage.end() ? it->second : bytes32{};
}

template <class Storage>
void EthereumHost<Storage>::set_transient_storage(
    const address& addr, const bytes32& key, const bytes32& value) noexcept
{
    auto& slot = m_state.get(addr).transient_storage[key];
    m_state.journal_transient_storage_change(addr, key, slot);
    slot = value;
}

template <class Storage>
evmc::uint256be EthereumHost<Storage>::get_balance(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    return (acc != nullptr) ? evm::toEvmcBE<evmc::uint256be>(acc->balance) : evmc::uint256be{};
}

namespace eth_host_detail
{
/// Check if an existing account is the "create collision"
/// as defined in the [EIP-7610](https://eips.ethereum.org/EIPS/eip-7610).
[[nodiscard]] inline bool is_create_collision(const EthAccount& acc) noexcept
{
    if (acc.nonce != 0)
        return true;
    if (acc.code_hash != EthAccount::EMPTY_CODE_HASH)
        return true;
    if (acc.has_initial_storage)
        return true;

    // The hot storage is ignored because it can contain elements from access list.
    assert(!acc.destructed && "untested");
    return false;
}
}  // namespace eth_host_detail

template <class Storage>
size_t EthereumHost<Storage>::get_code_size(const address& addr) const noexcept
{
    const auto raw_code = m_state.get_code(addr);
    return raw_code.size();
}

template <class Storage>
bytes32 EthereumHost<Storage>::get_code_hash(const address& addr) const noexcept
{
    const auto* const acc = m_state.find(addr);
    if (acc == nullptr || acc->is_empty())
        return {};

    return acc->code_hash;
}

template <class Storage>
size_t EthereumHost<Storage>::copy_code(const address& addr, size_t code_offset,
    uint8_t* buffer_data, size_t buffer_size) const noexcept
{
    const auto code = m_state.get_code(addr);
    const auto code_slice = code.substr(std::min(code_offset, code.size()));
    const auto num_bytes = std::min(buffer_size, code_slice.size());
    std::copy_n(code_slice.begin(), num_bytes, buffer_data);
    return num_bytes;
}

template <class Storage>
bool EthereumHost<Storage>::selfdestruct(const address& addr, const address& beneficiary) noexcept
{
    if (m_state.find(beneficiary) == nullptr)
        m_state.journal_create(beneficiary, false);
    auto& acc = m_state.get(addr);
    const auto balance = acc.balance;
    auto& beneficiary_acc = m_state.touch(beneficiary);

    m_state.journal_balance_change(beneficiary, beneficiary_acc.balance);
    m_state.journal_balance_change(addr, balance);

    if (m_rev >= EVMC_CANCUN && !acc.just_created)
    {
        // EIP-6780:
        // "SELFDESTRUCT is executed in a transaction that is not the same
        // as the contract invoking SELFDESTRUCT was created"
        acc.balance = 0;
        beneficiary_acc.balance += balance;  // Keep balance if acc is the beneficiary.

        // Return "selfdestruct not registered".
        // In practice this affects only refunds before Cancun.
        return false;
    }

    // Transfer may happen multiple times per single account as account's balance
    // can be increased with a call following previous selfdestruct.
    beneficiary_acc.balance += balance;
    acc.balance = 0;  // Zero balance if acc is the beneficiary.

    // Mark the destruction if not done already.
    if (!acc.destructed)
    {
        m_state.journal_destruct(addr);
        acc.destructed = true;
        return true;
    }
    return false;
}

template <class Storage>
std::optional<evmc_message> EthereumHost<Storage>::prepare_message(evmc_message msg) noexcept
{
    assert(msg.kind != EVMC_EOFCREATE);
    if (msg.depth == 0 || msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
    {
        auto& sender_acc = m_state.get(msg.sender);

        // EIP-2681 (already checked for depth 0 during transaction validation).
        if (sender_acc.nonce == EthAccount::NonceMax)
            return {};  // Light early exception.

        if (msg.depth != 0)
        {
            m_state.journal_bump_nonce(msg.sender);
            ++sender_acc.nonce;  // Bump sender nonce.
        }

        if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
        {
            // Compute and set the address of the account being created.
            assert(evmc::address{msg.recipient} == address{});
            assert(evmc::address{msg.code_address} == address{});
            // Nonce was already incremented, but creation calculation needs non-incremented value
            assert(sender_acc.nonce != 0);
            const auto creation_sender_nonce = sender_acc.nonce - 1;
            if (msg.kind == EVMC_CREATE)
                msg.recipient = evm::compute_create_address(msg.sender, creation_sender_nonce);
            else
            {
                assert(msg.kind == EVMC_CREATE2);
                msg.recipient = evm::compute_create2_address(
                    msg.sender, msg.create2_salt, {msg.input_data, msg.input_size});
            }

            // By EIP-2929, the access to new created address is never reverted.
            access_account(msg.recipient);
        }
    }

    return msg;
}

template <class Storage>
evmc::Result EthereumHost<Storage>::create(const evmc_message& msg) noexcept
{
    assert(msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2);

    auto* new_acc = m_state.find(msg.recipient);
    const bool new_acc_exists = new_acc != nullptr;
    if (!new_acc_exists)
        new_acc = &m_state.insert(msg.recipient);
    else if (eth_host_detail::is_create_collision(*new_acc))
        return evmc::Result{EVMC_FAILURE};  // TODO: Add EVMC errors for creation failures.
    m_state.journal_create(msg.recipient, new_acc_exists);

    assert(new_acc != nullptr);
    assert(new_acc->nonce == 0);

    if (m_rev >= EVMC_SPURIOUS_DRAGON)
        new_acc->nonce = 1;  // No need to journal: create revert will 0 the nonce.

    new_acc->just_created = true;

    auto& sender_acc = m_state.get(msg.sender);  // TODO: Duplicated account lookup.
    const auto value = evm::fromEvmcBE(msg.value);
    assert(sender_acc.balance >= value && "EVM must guarantee balance");
    m_state.journal_balance_change(msg.sender, sender_acc.balance);
    m_state.journal_balance_change(msg.recipient, new_acc->balance);
    sender_acc.balance -= value;
    new_acc->balance += value;  // The new account may be prefunded.

    auto create_msg = msg;
    create_msg.input_data = nullptr;
    create_msg.input_size = 0;
    const bytes_view initcode{msg.input_data, msg.input_size};
    auto result = m_vm.execute(*this, m_rev, create_msg, initcode.data(), initcode.size());
    if (result.status_code != EVMC_SUCCESS)
    {
        result.create_address = msg.recipient;
        return result;
    }

    auto gas_left = result.gas_left;
    assert(gas_left >= 0);

    const bytes_view code{result.output_data, result.output_size};

    if (m_rev >= EVMC_SPURIOUS_DRAGON && code.size() > evmone::MAX_CODE_SIZE)
        return evmc::Result{EVMC_FAILURE};

    // Code deployment cost.
    const auto cost = std::ssize(code) * 200;
    gas_left -= cost;
    if (gas_left < 0)
    {
        return (m_rev == EVMC_FRONTIER) ?
                   evmc::Result{EVMC_SUCCESS, result.gas_left, result.gas_refund, msg.recipient} :
                   evmc::Result{EVMC_FAILURE};
    }

    if (!code.empty())
    {
        // EIP-3541: Reject new contract code starting with the 0xEF byte.
        if (m_rev >= EVMC_LONDON && code[0] == 0xEF)
            return evmc::Result{EVMC_CONTRACT_VALIDATION_FAILURE};

        new_acc->code_hash = evm::keccak256(code);
        new_acc->code = code;
        new_acc->code_changed = true;
    }

    return evmc::Result{result.status_code, gas_left, result.gas_refund, msg.recipient};
}

template <class Storage>
evmc::Result EthereumHost<Storage>::execute_message(const evmc_message& msg) noexcept
{
    assert(msg.kind != EVMC_EOFCREATE);
    if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2)
        return create(msg);

    if (msg.kind == EVMC_CALL)
    {
        const auto exists = m_state.find(msg.recipient) != nullptr;
        if (!exists)
            m_state.journal_create(msg.recipient, exists);
    }

    if (msg.kind == EVMC_CALL)
    {
        if (evmc::is_zero(msg.value))
            m_state.touch(msg.recipient);
        else
        {
            // We skip touching if we send value, because account cannot end up empty.
            // It will either have value, or code that transfers this value out, or will be
            // selfdestructed anyway.
            auto& dst_acc = m_state.get_or_insert(msg.recipient);

            // Transfer value: sender → recipient.
            // The sender's balance is already checked therefore the sender account must exist.
            const auto value = evm::fromEvmcBE(msg.value);
            assert(m_state.get(msg.sender).balance >= value);
            m_state.journal_balance_change(msg.sender, m_state.get(msg.sender).balance);
            m_state.journal_balance_change(msg.recipient, dst_acc.balance);
            m_state.get(msg.sender).balance -= value;
            dst_acc.balance += value;
        }
    }

    // Calls to precompile address via EIP-7702 delegation execute empty code instead of precompile.
    if ((msg.flags & EVMC_DELEGATED) == 0 && evm::is_precompile(m_rev, msg.code_address))
        return evm::call_precompile(m_rev, msg);

    // TODO: get_code() performs the account lookup. Add a way to get an account with code?
    const auto code = m_state.get_code(msg.code_address);
    if (code.empty())
        return evmc::Result{EVMC_SUCCESS, msg.gas};  // Skip trivial execution.

    return m_vm.execute(*this, m_rev, msg, code.data(), code.size());
}

template <class Storage>
evmc::Result EthereumHost<Storage>::call(const evmc_message& orig_msg) noexcept
{
    const auto msg = prepare_message(orig_msg);
    if (!msg.has_value())
        return evmc::Result{EVMC_FAILURE, orig_msg.gas};  // Light exception.

    const auto logs_checkpoint = m_logs.size();
    const auto state_checkpoint = m_state.checkpoint();

    auto result = execute_message(*msg);

    if (result.status_code != EVMC_SUCCESS)
    {
        static constexpr auto addr_03 = 0x03_address;
        auto* const acc_03 = m_state.find(addr_03);
        const auto is_03_touched = acc_03 != nullptr && acc_03->erase_if_empty;

        // Revert.
        m_state.rollback(state_checkpoint);
        m_logs.resize(logs_checkpoint);

        // The 0x03 quirk: the touch on this address is never reverted.
        if (is_03_touched && m_rev >= EVMC_SPURIOUS_DRAGON)
            m_state.touch(addr_03);
    }
    return result;
}

template <class Storage>
evmc_tx_context EthereumHost<Storage>::get_tx_context() const noexcept
{
    // EIP-1559 base fee, revision-adjusted here: pre-London blocks have no base
    // fee, and the host must not report a nonzero one (GASPRICE). This is the
    // same gate as split-4/4's fee accounting (EthereumTransition.h), so both
    // consumers agree on the field's precondition.
    const auto base_fee = (m_rev >= EVMC_LONDON) ? m_block.base_fee : 0;

    // TODO: The effective gas price is already computed in transaction validation.
    const auto max_gas_price = ethMaxGasPrice(m_tx, m_callParams);
    const auto max_priority_gas_price = ethMaxPriorityGasPrice(m_tx, m_callParams);
    assert(max_gas_price >= base_fee || max_gas_price == 0);
    const auto priority_gas_price = std::min(max_priority_gas_price, max_gas_price - base_fee);
    const auto effective_gas_price = base_fee + priority_gas_price;

    const auto sender = ethSender(m_tx);

    return evmc_tx_context{
        evm::toEvmcBE<evmc::uint256be>(effective_gas_price),  // By EIP-1559.
        sender,
        m_block.coinbase,
        m_block.number,
        m_block.timestamp,
        m_block.gas_limit,
        m_block.prev_randao,
        evmc::uint256be{static_cast<uint64_t>(m_chainId)},  // Chain ID (EIP-155).
        evmc::uint256be{base_fee},
        evm::toEvmcBE<evmc::uint256be>(m_block.blob_base_fee.value_or(0)),
        m_blobHashes.data(),
        m_blobHashes.size(),
        nullptr,  // initcodes (TXCREATE) — not used by this executor.
        0,        // initcodes_count
    };
}

template <class Storage>
bytes32 EthereumHost<Storage>::get_block_hash(int64_t block_number) const noexcept
{
    if (m_blockHashLookup)
    {
        try
        {
            // m_block.number is the executing block's height (from the execution
            // context); pass it so the provider can bound the 256-ancestor window
            // without reading the current height from storage.
            return m_blockHashLookup(block_number, m_block.number);
        }
        catch (...)
        {
            // Never let a lookup failure cross the noexcept boundary. Zero is
            // also a legal BLOCKHASH answer (out-of-window), so log: a storage
            // failure would otherwise be indistinguishable from a legal miss.
            BCOS_LOG(ERROR) << LOG_DESC("EthereumHost: block-hash lookup failed")
                            << LOG_KV("blockNumber", block_number);
            return {};
        }
    }
    BCOS_LOG(ERROR) << LOG_DESC("EthereumHost: block-hash lookup not injected")
                    << LOG_KV("blockNumber", block_number);
    return {};
}

template <class Storage>
void EthereumHost<Storage>::emit_log(const address& addr, const uint8_t* data, size_t data_size,
    const bytes32 topics[], size_t topics_count) noexcept
{
    m_logs.push_back({addr, {data, data_size}, {topics, topics + topics_count}});
}

template <class Storage>
evmc_access_status EthereumHost<Storage>::access_account(const address& addr) noexcept
{
    if (m_rev < EVMC_BERLIN)
        return EVMC_ACCESS_COLD;  // Ignore before Berlin.

    EthAccount fresh;
    fresh.erase_if_empty = true;
    auto& acc = m_state.get_or_insert(addr, std::move(fresh));

    if (acc.access_status == EVMC_ACCESS_WARM || evm::is_precompile(m_rev, addr))
        return EVMC_ACCESS_WARM;

    m_state.journal_access_account(addr);
    acc.access_status = EVMC_ACCESS_WARM;
    return EVMC_ACCESS_COLD;
}

template <class Storage>
evmc_access_status EthereumHost<Storage>::access_storage(
    const address& addr, const bytes32& key) noexcept
{
    auto& storage_slot = m_state.get_storage(addr, key);
    m_state.journal_storage_change(addr, key, storage_slot);
    return std::exchange(storage_slot.access_status, EVMC_ACCESS_WARM);
}

}  // namespace bcos::executor_v1::eth
