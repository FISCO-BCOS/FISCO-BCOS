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
/// The thrown type stays bcos::evm::engine::OpStorageError from OpCommon.h
/// (unchanged type, unchanged namespace — the INVALID/-32603 classification
/// on the caller side keys on it).

#pragma once

#include <ethereum-executor/EthereumState.h>
#include <opstack-executor/OpCommon.h>  // bcos::evm::engine::OpStorageError
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;

/// Block-wide first-error recorder shared by every per-tx EthereumState
/// instance of one block (mutex-guarded, first-write-wins — the block-level
/// check observes the FIRST error across all instances).
struct OpStorageErrorSlot
{
    /// noexcept: called from EthereumState's noexcept read path.
    void record(std::exception_ptr error) noexcept
    {
        if (error == nullptr)
            return;
        std::lock_guard const lock(mutex);
        if (firstError == nullptr)
            firstError = error;
    }

    [[nodiscard]] bool poisoned() const
    {
        std::lock_guard const lock(mutex);
        return firstError != nullptr;
    }

    /// what() of the first recorded error ("unknown storage error" for a
    /// non-std::exception thrower). Empty when not poisoned.
    [[nodiscard]] std::string firstErrorMessage() const
    {
        std::exception_ptr error;
        {
            std::lock_guard const lock(mutex);
            error = firstError;
        }
        if (error == nullptr)
            return {};
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unknown storage error";
        }
    }

    mutable std::mutex mutex;
    std::exception_ptr firstError;
};

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

/// Storage2 wrapper that records EVERY escaping storage2-layer exception into
/// the shared slot before rethrowing (the write-side counterpart of
/// EthereumState's swallowed-read reporting).
///
/// Why it exists: the new OP executor (OpEthExecutor) runs runTransaction /
/// opRunDeposit, whose only storage2 WRITES happen inside the final
/// applyToStorage — every earlier storage read goes through EthereumState's
/// noexcept wrappers (swallowed + recorded). On the legacy path the write-back
/// (Storage2State::applyDiff) was wrapped separately so a write-back fault left
/// as OpStorageError (-32603), never INVALID. With the fused new-layer
/// transition the same classification is recovered by recording here: an
/// escaping storage2 fault is then visible via slot->poisoned() at the catch
/// site and reclassified to OpStorageError, while consensus/policy throws never
/// touch the slot and keep their INVALID classification.
///
/// Placement: view ← OpFaultRecordingStorage ← Rollbackable ← EthereumState.
/// Known limitation: range() returns the underlying iterator; a fault that
/// surfaces lazily from iterator.next() (not from the range() call itself) does
/// not pass through this wrapper. The only such consumer on the OP path is
/// clearAccountStorage/hasStorageImpl — the latter is already inside
/// EthereumState's swallowing readAccount wrapper (recorded via the handler),
/// the former is a phase-2 selfdestruct cleanup edge.
template <class Storage>
class OpFaultRecordingStorage
{
public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    /// See-through for type-level traits (Rollbackable::StorageType walkers).
    using StorageType = Storage;

    OpFaultRecordingStorage(Storage& storage, std::shared_ptr<OpStorageErrorSlot> slot) noexcept
      : m_storage(storage), m_slot(std::move(slot))
    {}

    auto readOne(auto key, auto&&... args) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key), decltype(args)...>>>
    {
        try
        {
            co_return co_await storage2::readOne(
                m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto readSome(auto&& keys, auto&&... args) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys), decltype(args)...>>>
    {
        try
        {
            co_return co_await storage2::readSome(m_storage.get(),
                std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto writeOne(auto key, auto value, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteOne, Storage&,
            decltype(key), decltype(value), decltype(args)...>>>
    {
        using Return = task::AwaitableReturnType<std::invoke_result_t<storage2::WriteOne, Storage&,
            decltype(key), decltype(value), decltype(args)...>>;
        try
        {
            if constexpr (std::is_void_v<Return>)
            {
                co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value),
                    std::forward<decltype(args)>(args)...);
            }
            else
            {
                co_return co_await storage2::writeOne(m_storage.get(), std::move(key),
                    std::move(value), std::forward<decltype(args)>(args)...);
            }
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto writeSome(auto&& keyValues, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteSome, Storage&,
            decltype(keyValues), decltype(args)...>>>
    {
        using Return = task::AwaitableReturnType<std::invoke_result_t<storage2::WriteSome,
            Storage&, decltype(keyValues), decltype(args)...>>;
        try
        {
            if constexpr (std::is_void_v<Return>)
            {
                co_await storage2::writeSome(m_storage.get(),
                    std::forward<decltype(keyValues)>(keyValues),
                    std::forward<decltype(args)>(args)...);
            }
            else
            {
                co_return co_await storage2::writeSome(m_storage.get(),
                    std::forward<decltype(keyValues)>(keyValues),
                    std::forward<decltype(args)>(args)...);
            }
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto removeOne(auto key, auto&&... args) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::RemoveOne, Storage&, decltype(key), decltype(args)...>>>
    {
        try
        {
            co_return co_await storage2::removeOne(
                m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto removeSome(auto&& keys, auto&&... args) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::RemoveSome, Storage&, decltype(keys), decltype(args)...>>>
    {
        try
        {
            co_return co_await storage2::removeSome(m_storage.get(),
                std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto existsOne(auto key, auto&&... args) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ExistsOne, Storage&, decltype(key), decltype(args)...>>>
    {
        try
        {
            co_return co_await storage2::existsOne(
                m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    /// range() forwards the underlying iterator; a fault surfacing lazily from
    /// iterator.next() (not from the range() call itself) does NOT pass
    /// through this wrapper (known limitation, see file header comment).
    auto range(auto&&... args)
        -> task::Task<storage2::ReturnType<
            std::invoke_result_t<storage2::Range, Storage&, decltype(args)...>>>
    {
        try
        {
            co_return co_await storage2::range(
                m_storage.get(), std::forward<decltype(args)>(args)...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    /// Raw passthrough reads (Rollbackable's journaling preimage capture
    /// requires them on the wrapped storage).
    auto readOneRaw(auto key, auto... tags)
        -> task::Task<task::AwaitableReturnType<decltype(
            std::declval<Storage&>().readOneRaw(std::move(key), tags...))>>
        requires executor_v1::HasReadOneRaw<Storage>
    {
        try
        {
            co_return co_await m_storage.get().readOneRaw(std::move(key), tags...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

    auto readSomeRaw(auto&& keys, auto... tags)
        -> task::Task<task::AwaitableReturnType<decltype(std::declval<Storage&>().readSomeRaw(
            std::forward<decltype(keys)>(keys), tags...))>>
        requires executor_v1::HasReadSomeRaw<Storage>
    {
        try
        {
            co_return co_await m_storage.get().readSomeRaw(
                std::forward<decltype(keys)>(keys), tags...);
        }
        catch (...)
        {
            recordCurrent();
            throw;
        }
    }

private:
    void recordCurrent() noexcept
    {
        if (m_slot)
        {
            m_slot->record(std::current_exception());
        }
    }

    std::reference_wrapper<Storage> m_storage;
    std::shared_ptr<OpStorageErrorSlot> m_slot;
};
}  // namespace bcos::executor_v1::opstack
