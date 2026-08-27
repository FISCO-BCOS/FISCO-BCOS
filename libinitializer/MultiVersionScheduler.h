#pragma once

#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-utilities/Exceptions.h"

namespace bcos::scheduler_v1
{

/// Thrown when setVersion() is asked to select a negative executor version.
DERIVE_BCOS_EXCEPTION(ExecutorVersionNotSupported);

/// The executor version that selects the pure-Ethereum EthereumExecutor
/// (ethereum-executor). It is index 2 of MultiVersionScheduler's scheduler array.
/// Versions >= this all select the v2 executor (setVersion saturates), leaving
/// room above 2 for a future executor without a schema change.
/// The canonical value lives in bcos-framework/ledger (so lower layers can gate on
/// it without depending on libinitializer); this keeps the scheduler_v1 spelling.
constexpr static int ETHEREUM_EXECUTOR_VERSION = ledger::ETHEREUM_EXECUTOR_VERSION;

/// executor_version >= this enters OP mode.
constexpr static int OPSTACK_EXECUTOR_VERSION = ledger::OPSTACK_EXECUTOR_VERSION;
/// Version ordering invariant: OP sits strictly above the Ethereum executor.
static_assert(OPSTACK_EXECUTOR_VERSION > ETHEREUM_EXECUTOR_VERSION,
    "OPSTACK_EXECUTOR_VERSION must be strictly greater than ETHEREUM_EXECUTOR_VERSION");

class MultiVersionScheduler : public bcos::scheduler::SchedulerInterface
{
private:
    // Slots: 0 legacy, 1 baseline, 2 EthereumExecutor, 3 OP (same instance as the engine delegate).
    static constexpr size_t SUPPORTED_EXECUTOR_VERSION_COUNT = 4;

    std::array<scheduler::SchedulerInterface::Ptr, SUPPORTED_EXECUTOR_VERSION_COUNT> m_schedulers;
    int m_currentIndex;

    bcos::scheduler::SchedulerInterface& getScheduler();

public:
    bcos::scheduler::SchedulerInterface& scheduler(int version);

    MultiVersionScheduler(
        std::array<scheduler::SchedulerInterface::Ptr, SUPPORTED_EXECUTOR_VERSION_COUNT>
            schedulers);

    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool sysBlock)>
            callback) override;

    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr)> callback) override;

    void status([[maybe_unused]] std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)>
            callback) override;

    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override;

    /// eth_call pinned at a block height (M13.2 / OP historical call): forward to the
    /// selected scheduler. Without this override the interface default would silently drop
    /// the height and serve the latest state — a wrong answer, not an error.
    void callAtBlock(protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override;

    void reset([[maybe_unused]] std::function<void(Error::Ptr)> callback) override;

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override;

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(std::string_view address,
        std::string_view key, bcos::protocol::BlockNumber number) override;

    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify,
        [[maybe_unused]] std::function<void(Error::Ptr)> callback) override;

    void stop() override;

    void setVersion(int version, ledger::LedgerConfig::Ptr ledgerConfig) override;
};
}  // namespace bcos::scheduler_v1