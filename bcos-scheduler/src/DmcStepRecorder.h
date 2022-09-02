//
// Created by Jimmy Shi on 2022/6/6.
//

#pragma once
#include <bcos-framework/executor/ExecutionMessage.h>
#include <atomic>
#include <sstream>
#include <string>
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

    uint32_t getRound() { return m_round; }

    std::string toHex(uint32_t x)
    {
        std::stringstream ss;
        ss << std::setbase(16) << x;  // to hex
        return ss.str();
    }

    std::string getChecksum()
    {
        return toHex(m_checksum) + ":" + getSendChecksum() + ":" + getReceiveChecksum();
    }

    std::string getSendChecksum() { return toHex(m_sendChecksum); }

    std::string getReceiveChecksum() { return toHex(m_receiveChecksum); }

    void clear()
    {
        m_sendChecksum = 0;
        m_receiveChecksum = 0;
        m_checksum = 0;
        m_round = 0;
    }

    std::string dumpAndClearChecksum()
    {
        std::string checksum = getChecksum();
        clear();
        return checksum;
    }

    // TODO: record execute history


private:
    std::atomic<uint32_t> m_sendChecksum = 0;
    std::atomic<uint32_t> m_receiveChecksum = 0;
    uint32_t m_round = 0;
    uint32_t m_checksum = 0;

    uint32_t getMessageChecksum(const protocol::ExecutionMessage::UniquePtr& message);
    uint32_t getAddressChecksum(std::string_view address);
    uint32_t getRecordSum(std::string_view address, uint32_t id,
        const protocol::ExecutionMessage::UniquePtr& message);
};
}  // namespace bcos::scheduler
