#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-utilities/FixedBytes.h"
#include <indicators/block_progress_bar.hpp>
#include <indicators/cursor_control.hpp>
#include <indicators/multi_progress.hpp>

namespace bcos::sample
{

bcos::bytes getContractBin();
std::string_view getContractABI();
long currentTime();

class Collector
{
private:
    long m_startTime;
    int m_count;
    std::string m_title;
    indicators::BlockProgressBar m_sendProgressBar;
    indicators::BlockProgressBar m_receiveProgressBar;
    indicators::MultiProgress<indicators::BlockProgressBar, 2> m_progressBar;

    long sendElapsed = std::numeric_limits<long>::max();
    std::atomic_long m_allTimeCost = 0;
    std::atomic_int m_sended = 0;
    std::atomic_int m_finished = 0;
    std::atomic_int m_failed = 0;

public:
    Collector(int count, std::string title);

    void finishSend();
    void send(bool success, long elapsed);
    void receive(bool success, long elapsed);
    void report();
};

std::string parseRevertMessage(bcos::bytesConstRef output, bcos::crypto::Hash::Ptr hashImpl);

}  // namespace bcos::sample