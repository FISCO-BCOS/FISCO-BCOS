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
/// This is the L1 counterpart of the OP lane's OpStorageErrorGuard
/// (opstack-executor/OpStorageErrorGuard.h): same slot pattern, but the L1
/// slot travels through the executor's BlockContext instead of an
/// executor-wide member, so two blocks executing concurrently on one shared
/// executor never share a recorder.

#pragma once

#include "EthereumState.h"
#include <bcos-utilities/Exceptions.h>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

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

    /// Block-boundary check. Throws EthStorageError (a build failure / SYNCING
    /// / -32603 on the caller side, never INVALID) when any read on any
    /// instance sharing the slot failed.
    void throwIfPoisoned(std::string_view context = {}) const
    {
        if (!poisoned())
            return;
        BOOST_THROW_EXCEPTION(
            EthStorageError{} << errinfo_comment{
                std::string(context) + "l1 block: storage read failed: " + firstErrorMessage()});
    }

    mutable std::mutex mutex;
    std::exception_ptr firstError;
};

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
}  // namespace bcos::executor_v1::eth
