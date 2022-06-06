//
// Created by Jimmy Shi on 2022/6/6.
//

#include "DmcStepRecorder.h"

using namespace bcos::scheduler;

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

    for (int i = 0; i < 4; i++)
    {
        ret <<= 8;
        ret |= (uint8_t)address[i];
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
#pragma omp parallel for schedule(static, 1000) reduction(+ : sum)
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
#pragma omp parallel for schedule(static, 1000) reduction(+ : sum)
    for (size_t id = 0; id < size; id++)
    {
        sum += getRecordSum(address, id, message[id]);
    }

    m_receiveChecksum.fetch_add(sum);
}


void DmcStepRecorder::nextDmcRound()
{
    m_checksum = m_sendChecksum.fetch_xor(m_receiveChecksum.fetch_xor(m_checksum));
}
