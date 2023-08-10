#pragma once
#include "bcos-utilities/FixedBytes.h"
#include <boost/timer/progress_display.hpp>

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
    boost::timer::progress_display m_progress;

    long sendElapsed = std::numeric_limits<long>::max();
    std::atomic_long m_allTimeCost = 0;
    std::atomic_int m_finished = 0;
    std::atomic_int m_failed = 0;

public:
    Collector(int count, std::string title);

    void finishSend();
    void receive(bool success, long elapsed);
    void report() const;
};

}  // namespace bcos::sample