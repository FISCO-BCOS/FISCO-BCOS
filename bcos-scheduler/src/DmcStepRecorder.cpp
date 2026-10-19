//
// Created by Jimmy Shi on 2022/6/6.
//

#include "DmcStepRecorder.h"

using namespace bcos::scheduler;

uint32_t DmcStepRecorder::getRound()
{
    return m_round;
}

std::string DmcStepRecorder::toHex(uint32_t x)
{
    std::stringstream ss;
    ss << std::setbase(16) << x;
    return ss.str();
}

std::string DmcStepRecorder::getChecksum()
{
    return toHex(m_checksum) + ":" + getSendChecksum() + ":" + getReceiveChecksum() + ":" +
           std::to_string(m_round);
}

std::string DmcStepRecorder::getSendChecksum()
{
    return toHex(m_sendChecksum);
}

std::string DmcStepRecorder::getReceiveChecksum()
{
    return toHex(m_receiveChecksum);
}

void DmcStepRecorder::clear()
{
    m_sendChecksum = 0;
    m_receiveChecksum = 0;
    m_checksum = 0;
    m_round = 0;
}

std::string DmcStepRecorder::dumpAndClearChecksum()
{
    std::string checksum = getChecksum();
    clear();
    return checksum;
}

uint32_t DmcStepRecorder::getMessageChecksum(const protocol::ExecutionMessage::UniquePtr& message)
{
    bool staticCall = message->staticCall();

    uint32_t type = (uint32_t)message->type();
    uint32_t contextID = (uint32_t)message->contextID();
    uint32_t seq = (uint32_t)message->seq();
    uint32_t from = getAddressChecksum(message->from());
    uint32_t to = getAddressChecksum(message->to());
    uint32_t origin = getAddressChecksum(message->origin());

    uint32_t sufferSum = (~from + type) * (~to + contextID) ^ (~origin + seq);

    return staticCall ? sufferSum : ~sufferSum;
}

uint32_t DmcStepRecorder::getAddressChecksum(std::string_view address)
{
    uint32_t ret = 0;

    // Only the leading 4 bytes take part in the checksum, so read no further than
    // what the address actually holds: a Solidity contract-creation transaction
    // reaches here with an empty to (BlockExecutive sets create and never calls
    // setTo), so every deploy executed through DMC hits this path, and test mocks
    // can carry short addresses too. Missing bytes are folded in as zero, which
    // keeps the value unchanged for real addresses (>= 4 bytes) and stays
    // deterministic for the short ones instead of reading out of bounds.
    auto length = std::min<size_t>(4, address.size());
    for (size_t i = 0; i < length; i++)
    {
        ret <<= 8;
        ret |= (uint8_t)address[i];
    }
    for (size_t i = length; i < 4; i++)
    {
        ret <<= 8;
    }
    return ret;
}

uint32_t DmcStepRecorder::getRecordSum(
    std::string_view address, uint32_t id, const protocol::ExecutionMessage::UniquePtr& message)
{
    uint32_t addrSum = getAddressChecksum(address);
    uint32_t msgSum = getMessageChecksum(message);

    return addrSum * msgSum * (id + 1);
}

void DmcStepRecorder::recordSend(
    std::string_view address, uint32_t id, const protocol::ExecutionMessage::UniquePtr& message)
{
    uint32_t sum = getRecordSum(address, id, message);

    m_sendChecksum.fetch_add(sum);
}

void DmcStepRecorder::recordReceive(
    std::string_view address, uint32_t id, const protocol::ExecutionMessage::UniquePtr& message)
{
    uint32_t sum = getRecordSum(address, id, message);

    m_receiveChecksum.fetch_add(sum);
}

void DmcStepRecorder::recordSends(
    std::string_view address, const std::vector<protocol::ExecutionMessage::UniquePtr>& message)
{
    size_t size = message.size();

    uint32_t sum = 0;
    // #pragma omp parallel for schedule(static, 1000) reduction(+ : sum)
    for (size_t id = 0; id < size; id++)
    {
        sum += getRecordSum(address, id, message[id]);
    }

    m_sendChecksum.fetch_add(sum);
}

void DmcStepRecorder::recordReceives(
    std::string_view address, const std::vector<protocol::ExecutionMessage::UniquePtr>& message)
{
    size_t size = message.size();
    uint32_t sum = 0;
    // #pragma omp parallel for schedule(static, 1000) reduction(+ : sum)
    for (size_t id = 0; id < size; id++)
    {
        sum += getRecordSum(address, id, message[id]);
    }

    m_receiveChecksum.fetch_add(sum);
}


void DmcStepRecorder::nextDmcRound()
{
    m_round++;
    m_checksum = m_sendChecksum.fetch_xor(m_receiveChecksum.fetch_xor(m_checksum)) + m_round;
}
