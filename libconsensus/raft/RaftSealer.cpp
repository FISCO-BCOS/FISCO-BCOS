#include "RaftSealer.h"

using namespace std;
using namespace dev;
using namespace dev::consensus;


void RaftSealer::start()
{
    m_raftEngine->start();
    Sealer::start();
}

void RaftSealer::stop()
{
    Sealer::stop();
    m_raftEngine->stop();
}

bool RaftSealer::shouldSeal()
{
    return Sealer::shouldSeal() && m_raftEngine->shouldSeal();
}

bool RaftSealer::reachBlockIntervalTime()
{
    return m_raftEngine->reachBlockIntervalTime();
}

void RaftSealer::handleBlock()
{
    resetSealingHeader(m_sealing.block.header());
    m_sealing.block.calTransactionRoot();

    RAFTSEALER_LOG(INFO) << "+++++++++++++++++++++++++++ [#Generating seal on]:  "
                         << "[blockNumber/txNum/hash]:  " << m_sealing.block.header().number()
                         << "/" << m_sealing.block.getTransactionSize() << "/"
                         << m_sealing.block.header().hash() << std::endl;
    bool succ = m_raftEngine->commit(m_sealing.block);
    if (!succ)
    {
        resetSealingBlock();
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }
}