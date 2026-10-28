//
// Created by Jimmy Shi on 2022/6/6.
//

#pragma once
#include <bcos-framework/executor/ExecutionMessage.h>
#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::scheduler
{

class DmcStepRecorder
{
    // Record DMC step for debugging inconsistency bug

public:
    using Ptr = std::shared_ptr<DmcStepRecorder>;

    void recordSend(std::string_view address, uint32_t id,
        const protocol::ExecutionMessage::UniquePtr& message);

    void recordReceive(std::string_view address, uint32_t id,
        const protocol::ExecutionMessage::UniquePtr& message);

    void recordSends(std::string_view address,
        const std::vector<protocol::ExecutionMessage::UniquePtr>& message);

    void recordReceives(std::string_view address,
        const std::vector<protocol::ExecutionMessage::UniquePtr>& message);

    /// A response handed to an executor resumed a live frame without the schedulerSeq
    /// hand-back. Recorded (not asserted) so release builds can observe the defect and
    /// a test can prove the check itself is wired.
    void recordSchedulerStampViolation(std::string_view address, int64_t seq, int type);
    uint32_t schedulerStampViolations() const { return m_schedulerStampViolations.load(); }

    /// A message handed to another DmcExecutor. `currentSeq` is the tx-context counter
    /// at the moment of the hand-off — the value the executor's create derivation must
    /// reproduce as f(currentSeq). Recorded for tests; the scheduler protocol itself is
    /// unchanged.
    struct ScheduleOutRecord
    {
        std::string contract;
        int64_t contextID = 0;
        int64_t seq = 0;
        int64_t schedulerSeq = -1;
        int64_t currentSeq = 0;
        std::string target;
        bool create = false;
        protocol::ExecutionMessage::Type type{};
    };
    /// Test diagnostics are opt-in; normal scheduling does not allocate or lock.
    void enableScheduleOutRecording(bool enabled = true)
    {
        m_recordScheduleOuts.store(enabled, std::memory_order_relaxed);
    }
    void recordScheduleOut(std::string_view contract, int64_t contextID, int64_t seq,
        int64_t schedulerSeq, int64_t currentSeq, std::string_view target, bool create,
        protocol::ExecutionMessage::Type type);
    std::vector<ScheduleOutRecord> scheduleOuts() const;

    void nextDmcRound();

    uint32_t getRound();

    std::string toHex(uint32_t x);

    std::string getChecksum();

    std::string getSendChecksum();

    std::string getReceiveChecksum();

    void clear();

    std::string dumpAndClearChecksum();

    // TODO: record execute history


private:
    std::atomic<uint32_t> m_sendChecksum = 0;
    std::atomic<uint32_t> m_receiveChecksum = 0;
    std::atomic<uint32_t> m_schedulerStampViolations = 0;
    std::atomic<bool> m_recordScheduleOuts = false;
    mutable std::mutex m_scheduleOutMutex;
    std::vector<ScheduleOutRecord> m_scheduleOuts;
    uint32_t m_round = 0;
    uint32_t m_checksum = 0;

    void clearChecksums();
    uint32_t getMessageChecksum(const protocol::ExecutionMessage::UniquePtr& message);
    uint32_t getAddressChecksum(std::string_view address);
    uint32_t getRecordSum(std::string_view address, uint32_t id,
        const protocol::ExecutionMessage::UniquePtr& message);
};
}  // namespace bcos::scheduler
