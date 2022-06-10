//
// Created by Jimmy Shi on 2022/6/6.
//

#pragma once
#include <bcos-framework/executor/ExecutionMessage.h>
#include <atomic>
#include <sstream>
#include <string_view>


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

    void nextDmcRound();

    std::string dumpAndClearChecksum()
    {
        m_sendChecksum = 0;
        m_receiveChecksum = 0;

        std::stringstream ss;
        ss << std::setbase(16) << m_checksum;  // to hex
        m_checksum = 0;
        return ss.str();
    }

    // TODO: record execute history


private:
    std::atomic<uint32_t> m_sendChecksum = 0;
    std::atomic<uint32_t> m_receiveChecksum = 0;
    uint32_t m_checksum = 0;

    uint32_t getMessageChecksum(const protocol::ExecutionMessage::UniquePtr& message);
    uint32_t getAddressChecksum(std::string_view address);
    uint32_t getRecordSum(std::string_view address, uint32_t id,
        const protocol::ExecutionMessage::UniquePtr& message);
};
}  // namespace bcos::scheduler
