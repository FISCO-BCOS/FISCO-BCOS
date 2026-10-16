/// @file OpStorageErrorGuard.h
/// @brief OP fail-loud storage channel over EthereumState's optional error
///        slot (EthereumState::setStorageErrorHandler).
///
/// The L1 semantics of EthereumState's synchronous read wrappers are
/// fail-safe: a failed read is swallowed and reported as "absent / empty".
/// On the OP consensus path a silent zero read would let a storage fault
/// masquerade as an insufficient balance or a wrong stateRoot, so the OP
/// block-execution driver injects a recorder into every per-tx
/// EthereumState instance and checks it at the block boundary — the
/// bcos-evm-free counterpart of the legacy path's SharedErrorSlot
/// (bcos-evm/adapter/Storage2State.h, op-geth's dbErr analogue).
///
/// The recorder itself is shared with the L1 lane: OpStorageErrorSlot is an
/// alias of eth::EthStorageErrorSlot and OpFaultRecordingStorage is an alias
/// of eth::FaultRecordingStorage (both in
/// ethereum-executor/EthStorageErrorGuard.h). What stays here is the OP-side
/// guard whose throwIfPoisoned keeps throwing
/// bcos::evm::engine::OpStorageError from OpCommon.h (unchanged type,
/// unchanged namespace — the INVALID/-32603 classification on the caller
/// side keys on it).

#pragma once

#include <ethereum-executor/EthStorageErrorGuard.h>
#include <ethereum-executor/EthereumState.h>
#include <opstack-executor/OpCommon.h>  // bcos::evm::engine::OpStorageError
#include <memory>
#include <string>
#include <string_view>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;

/// Block-wide first-error recorder shared by every per-tx EthereumState
/// instance of one block — the shared implementation lives in
/// ethereum-executor/EthStorageErrorGuard.h.
using OpStorageErrorSlot = eth::EthStorageErrorSlot;

/// Storage2 wrapper recording escaping storage2-layer exceptions into the
/// shared slot — the shared implementation lives in
/// ethereum-executor/EthStorageErrorGuard.h.
template <class Storage>
using OpFaultRecordingStorage = eth::FaultRecordingStorage<Storage>;

/// Injects a shared OpStorageErrorSlot into one EthereumState instance.
/// The block path recreates EthereumState per transaction, so the driver
/// keeps the guard's slot() and constructs one guard per instance; the
/// block-boundary throwIfPoisoned() then covers the whole block.
template <class Storage>
class OpStorageErrorGuard
{
public:
    explicit OpStorageErrorGuard(eth::EthereumState<Storage>& state,
        std::shared_ptr<OpStorageErrorSlot> slot = std::make_shared<OpStorageErrorSlot>()) noexcept
      : m_slot(std::move(slot))
    {
        auto slotCopy = m_slot;
        state.setStorageErrorHandler(
            [slotCopy](std::exception_ptr error) { slotCopy->record(error); });
    }

    OpStorageErrorGuard(const OpStorageErrorGuard&) = delete;
    OpStorageErrorGuard(OpStorageErrorGuard&&) = delete;
    OpStorageErrorGuard& operator=(const OpStorageErrorGuard&) = delete;
    OpStorageErrorGuard& operator=(OpStorageErrorGuard&&) = delete;
    ~OpStorageErrorGuard() = default;

    [[nodiscard]] bool poisoned() const { return m_slot->poisoned(); }

    /// The shared slot — pass it to the next per-tx guard so one block
    /// aggregates into one recorder.
    [[nodiscard]] const std::shared_ptr<OpStorageErrorSlot>& slot() const noexcept
    {
        return m_slot;
    }

    /// Block-boundary check. Throws bcos::evm::engine::OpStorageError
    /// (JSON-RPC -32603 on the caller side, never INVALID) when any read on
    /// any instance sharing the slot failed.
    void throwIfPoisoned(std::string_view context = {}) const
    {
        if (!m_slot->poisoned())
            return;
        throw bcos::evm::engine::OpStorageError(std::string(context) +
                                                "op-eth block: storage read failed: " +
                                                m_slot->firstErrorMessage());
    }

private:
    std::shared_ptr<OpStorageErrorSlot> m_slot;
};
}  // namespace bcos::executor_v1::opstack
