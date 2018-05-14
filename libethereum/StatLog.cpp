/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: StatLog.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "StatLog.h"
#include <stack>
#include <libdevcore/easylog.h>
#include "NodeConnParamsManagerApi.h"

using namespace std;
namespace dev
{
namespace eth 
{


uint64_t StatTxExecLogGuard::report_interval(60); // 1min
uint64_t LogFlowConstant::PBFTReportInterval(60); // imin
uint64_t LogFlowConstant::TxReportInterval(60); // imin
uint64_t LogConstant::BroadcastTxInterval(60); // imin
uint64_t LogConstant::BroadcastBlockInterval(60); // imin

inline string millisecondsToString(uint64_t&& milliseconds,int offset = 8) 
{
    uint16_t tmp[4];
    stringstream ss;
    tmp[3] = milliseconds % 1000;  // ms
    milliseconds /= 1000;
    tmp[2] = milliseconds % 60;  // s
    milliseconds /= 60;
    tmp[1] = milliseconds % 60;  // min
    milliseconds /= 60;
    tmp[0] = (milliseconds % 24 + offset) % 24; // hour
    ss << tmp[0] << ":" << tmp[1] << ":" << tmp[2] << ":" << tmp[3];
    return ss.str();
}

// pbft state flow 
// start -> seal -> exec -> collectSign -> collectCommit -> blkToChain -> destory
// or start -> exec -> collectSign -> collectCommit -> blkToChain -> destory
inline void clearAllStateMonitor(StatLogContext& context, string&& msg) 
{
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_SEAL, STAT_BLOCK_PBFT_SEAL, move(msg), 0, context.getCode());
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_EXEC, STAT_BLOCK_PBFT_EXEC, move(msg), 0, context.getCode());
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_SIGN, STAT_BLOCK_PBFT_SIGN, move(msg), 0, context.getCode());
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_COMMIT, STAT_BLOCK_PBFT_COMMIT, move(msg), 0, context.getCode());
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_BLKTOCHAIN, STAT_BLOCK_PBFT_CHAIN, move(msg), 0, context.getCode());
}

void StartBlockState::handle(StatLogContext& context, const string& str, void* ext) 
{
    bool isLeader = *(bool*)ext;
    if (context.str().empty())
        context << "[block]";  // start tag
    else 
        context << "[restart]";

    // state monitor
    clearAllStateMonitor(context, "init");

    if (isLeader) 
    {
        context << "[Leader]";
        context.changeState(std::make_unique<SealedState>());
        statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_SEAL, LogFlowConstant::PBFTReportInterval, context.getCode());
    }
    else 
    {
        context.changeState(std::make_unique<ExecedState>());
        statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_EXEC, LogFlowConstant::PBFTReportInterval, context.getCode());
    }
    // for final log
    context << " | start";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
}

void SealedState::handle(StatLogContext& context, const string& str, void* ext) 
{
    context << " | sealed";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // monitor
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_SEAL, STAT_BLOCK_PBFT_SEAL, "", 1, context.getCode());
    bool is_empty_blk = *(bool*)ext;
    
    if (!is_empty_blk) {
        statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_EXEC, LogFlowConstant::PBFTReportInterval, context.getCode());
    }

    context.changeState(std::make_unique<ExecedState>());
}

void ExecedState::handle(StatLogContext& context, const string& str, void* ext) 
{
    context << " | execed";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // monitor
    int code = *(int*)ext;
    if (code == 1) // empty block  不参与记时，把succes置-1，表示不记录此时的时间
    {
        statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_EXEC, STAT_BLOCK_PBFT_SEAL, "", -1, context.getCode());
    }
    else // normal block
    {
        statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_EXEC, STAT_BLOCK_PBFT_SEAL, "", 1, context.getCode());
        statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_SIGN, LogFlowConstant::PBFTReportInterval, context.getCode());
    }
    context.changeState(std::make_unique<CollectedSignState>());
}

void CollectedSignState::handle(StatLogContext& context, const string& str, void*) 
{
    context << " | signed";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // monitor
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_SIGN, STAT_BLOCK_PBFT_SIGN, "", 1, context.getCode());
    statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_COMMIT, LogFlowConstant::PBFTReportInterval, context.getCode());

    context.changeState(std::make_unique<CollectedCommitState>());
}

void CollectedCommitState::handle(StatLogContext& context, const string& str, void*) 
{
    context << " | commited";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // monitor
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_COMMIT, STAT_BLOCK_PBFT_COMMIT, "", 1, context.getCode());
    statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_BLKTOCHAIN, LogFlowConstant::PBFTReportInterval, context.getCode());

    context.changeState(std::make_unique<BlkToChainState>());
}

void BlkToChainState::handle(StatLogContext& context, const string& str, void*) 
{
    context << " | onChain";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // monitor
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_BLKTOCHAIN, STAT_BLOCK_PBFT_CHAIN, "", 1, context.getCode());

    context.changeState(std::make_unique<DestoriedState>());
    ((PBFTStatLog*)&context)->m_is_final = true; // 正常转换到了final状态
}

void ViewChangeStartState::handle(StatLogContext& context, const string& str, void*) 
{
    context << " | viewchange_start";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // clear all
    clearAllStateMonitor(context, "viewchange");
    statemonitor::recordStateByTimeStart(dev::StatCode::BLOCK_VIEWCHANG, LogFlowConstant::PBFTReportInterval, context.getCode());

    context.changeState(std::make_unique<ViewChangedState>());    
}

void ViewChangedState::handle(StatLogContext& context, const string& str, void*) 
{
    context << " | viewchanged";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    // monitor
    statemonitor::recordStateByTimeEnd(dev::StatCode::BLOCK_VIEWCHANG, STAT_BLOCK_PBFT_VIEWCHANGE, "", 1, context.getCode());
    context.changeState(std::make_unique<DestoriedState>());
    ((PBFTStatLog*)&context)->m_is_final = true;
}

void DestoriedState::handle(StatLogContext& context, const string& str, void* ext) 
{
    bool err = *(bool*)ext; 
    if (err) 
    {
        context << " error, wait a long time!";
        if (!str.empty())
            context << "[" << str << "]";   
    }
    
    string final_log = context.str();
    
    NormalStatLog() << final_log;
}
static int init = 0;
// tx
// init -> toChain -> destory
void TxInitState::handle(StatLogContext& context, const string& str, void*) 
{
    context << "[tx] start";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());
    
    statemonitor::recordStateByTimeStart(dev::StatCode::TX_TRACE, LogFlowConstant::TxReportInterval, context.getCode());
    context.changeState(std::make_unique<TxToChainState>());
}

void TxToChainState::handle(StatLogContext& context, const string& str, void* ext) 
{
    
    bool is_discarded = *(bool*)ext;
    if (is_discarded) 
        context << " | discarded";
    else
        context << " | onChain";
    if (!str.empty())
        context << "[" << str << "]";
    context << ": " << millisecondsToString(utcTime());

    statemonitor::recordStateByTimeEnd(dev::StatCode::TX_TRACE, STAT_TX_TRACE, "", 1, context.getCode());

    context.changeState(std::make_unique<TxDestoriedState>());
    ((TxStatLog*)&context)->m_is_final = true; 
}

void TxDestoriedState::handle(StatLogContext& context, const string& str, void* ext) 
{
    bool err = *(bool*)ext;
    if (err) 
    {
        context << " error, wait a long time!";
        if (!str.empty())
            context << "[" << str << "]";

        statemonitor::recordStateByTimeEnd(dev::StatCode::TX_TRACE, STAT_TX_TRACE, context.str(), 0, context.getCode());
    }   
    string final_log = context.str();

    NormalStatLog() << final_log;
}

void PBFTFlowLog(const u256& hash, const string& str, int code,  bool is_init) 
{
    DefaultPBFTStatLog::instance().log(hash, str, &code, is_init);
}

void PBFTFlowViewChangeLog(const u256& hash, const string& str) 
{
    PBFTStatLog& logContext = DefaultPBFTStatLog::instance().logContext(hash);
    if (logContext.isValidState()) 
    {
        logContext.changeToViewchangeState();
        DefaultPBFTStatLog::instance().log(hash, str); // call viewchangestart state handle
    }
}

void TxFlowLog(const u256& hash, const string& str, bool is_discarded, bool is_init) 
{
    DefaultTxtatLog::instance().log(hash, str, &is_discarded, is_init);
}

void BroadcastTxSizeLog(const h512 &node_id, size_t packet_size)
{
    u256 idx = u256(0);
    if (!NodeConnManagerSingleton::GetInstance().getIdx(node_id, idx))
    {
        NormalStatLog() << "[ERROR] try to get an unknown peer in BroadcastTxSizeLog id=" << node_id;
        return;
    }

    statemonitor::recordStateByTimeOnce((int)(dev::StatCode::BROADCAST_TX_SIZE + idx), LogConstant::BroadcastTxInterval, (double)packet_size, STAT_BROADCAST_TX_SIZE, "to idx=" + toString(idx));
}

void BroadcastBlockSizeLog(const h512 &node_id, size_t packet_size)
{
    u256 idx = u256(0);
    if (!NodeConnManagerSingleton::GetInstance().getIdx(node_id, idx))
    {
        NormalStatLog() << "[ERROR] try to get an unknown peer in BroadcastBlockSizeLog id=" << node_id;
        return;
    }

    statemonitor::recordStateByTimeOnce((int)(dev::StatCode::BROADCAST_BLOCK_SIZE + idx), LogConstant::BroadcastBlockInterval, (double)packet_size, STAT_BROADCAST_BLOCK_SIZE, "to idx=" + toString(idx));
}

}
}