/// @file EthStorageErrorGuard.h
/// @brief L1 fail-loud storage channel over EthereumState's optional error
///        slot (EthereumState::setStorageErrorHandler).
///
/// EthereumState's synchronous read wrappers are fail-safe: a failed read is
/// swallowed and reported as "absent / empty" (the evmc::Host interface is
/// noexcept, so the catch must stay). On the L1 block lanes (the EL builder
/// and the block verifier, both driven through
/// EthereumBlockVerifier::executeEthereumBlock) a silent zero read would let a
/// storage fault masquerade as an insufficient balance or a wrong stateRoot —
/// the build lane would commit a poisoned canonical block, the verify lane
/// would answer INVALID for a legal one. The executor's per-block BlockContext
/// therefore carries a shared EthStorageErrorSlot that every per-tx
/// EthereumState instance records into, and the lane checks the slot at the
/// block boundary: a poisoned slot throws EthStorageError, which the callers
/// map to a build failure / SYNCING / -32603 — never INVALID, never a commit.
///
/// The block-boundary throw lives outside the slot: the slot is shared with
/// the OP lane (opstack-executor aliases it as OpStorageErrorSlot), while each
/// lane throws its own error type (L1: EthStorageError below; OP:
/// bcos::evm::engine::OpStorageError in OpStorageErrorGuard.h). The OP lane's
/// OpFaultRecordingStorage alias also points at the FaultRecordingStorage
/// template at the bottom of this file.

#pragma once

#include "EthereumState.h"
#include <bcos-utilities/Exceptions.h>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace bcos::executor_v1::eth
{
DERIVE_BCOS_EXCEPTION(EthStorageError);

/// Block-wide first-error recorder shared by every per-tx EthereumState
/// instance of one block (mutex-guarded, first-write-wins — the block-level
/// check observes the FIRST error across all instances).
struct EthStorageErrorSlot
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

/// Block-boundary check. Throws EthStorageError (a build failure / SYNCING
/// / -32603 on the caller side, never INVALID) when any read on any instance
/// sharing the slot failed.
inline void throwEthStorageErrorIfPoisoned(
    const EthStorageErrorSlot& slot, std::string_view context = {})
{
    if (!slot.poisoned())
        return;
    BOOST_THROW_EXCEPTION(
        EthStorageError{} << errinfo_comment{
            std::string(context) + "l1 block: storage read failed: " + slot.firstErrorMessage()});
}

/// Installs the slot as @p state's storage-error handler (the recorder must
/// not throw — it runs inside EthereumState's noexcept read wrappers).
template <class Storage>
void installStorageErrorSlot(
    EthereumState<Storage>& state, std::shared_ptr<EthStorageErrorSlot> slot) noexcept
{
    if (!slot)
        return;
    state.setStorageErrorHandler(
        [slot = std::move(slot)](std::exception_ptr error) { slot->record(error); });
}

/// Storage2 wrapper that records EVERY escaping storage2-layer exception into
/// the shared slot before rethrowing (the write-side counterpart of
/// EthereumState's swallowed-read reporting).
///
/// Why it exists: the executors run runTransaction / opRunDeposit, whose only
/// storage2 WRITES happen inside the final applyToStorage — every earlier
/// storage read goes through EthereumState's noexcept wrappers (swallowed +
/// recorded). On the legacy OP path the write-back (Storage2State::applyDiff)
/// was wrapped separately so a write-back fault left as OpStorageError
/// (-32603), never INVALID. With the fused new-layer transition the same
/// classification is recovered by recording here: an escaping storage2 fault
/// is then visible via slot->poisoned() at the catch site and reclassified to
/// the lane's storage error, while consensus/policy throws never touch the
/// slot and keep their INVALID classification.
///
/// Placement: view ← FaultRecordingStorage ← Rollbackable ← EthereumState.
/// Known limitation: range() returns the underlying iterator; a fault that
/// surfaces lazily from iterator.next() (not from the range() call itself) does
/// not pass through this wrapper. The only such consumer on the OP path is
/// clearAccountStorage/hasStorageImpl — the latter is already inside
/// EthereumState's swallowing readAccount wrapper (recorded via the handler),
/// the former is a phase-2 selfdestruct cleanup edge.
///
/// Shared with the OP lane: opstack-executor aliases this as
/// OpFaultRecordingStorage (opstack-executor/OpStorageErrorGuard.h).
template <class Storage>
class FaultRecordingStorage
{
public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    /// See-through for type-level traits (Rollbackable::StorageType walkers).
    using StorageType = Storage;

    FaultRecordingStorage(Storage& storage, std::shared_ptr<EthStorageErrorSlot> slot) noexcept
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
            co_return co_await storage2::removeSome(
                m_storage.get(), std::forward<decltype(keys)>(keys), std::forward<decltype(args)>(args)...);
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
    std::shared_ptr<EthStorageErrorSlot> m_slot;
};
}  // namespace bcos::executor_v1::eth
