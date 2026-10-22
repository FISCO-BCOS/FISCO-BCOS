/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */
#pragma once

#include <string>

namespace bcos::ops::test
{
// Lines copied verbatim from the log files of a local 4-node AIR chain (2026-10-10, node0/node1/
// node2 of .scratch/log-cleanup-chain, log_2026101002.*.log), in time order: the startup version
// line, a peer going away and coming back, one sync segment (node1 downloading 15-16), a
// viewchange 0 -> 3 with node0 skipping seals while it is not the leader, and the PBFT round of
// block 19 on node2 (the real order has CheckpointSent before ProposalExecuted, and the quorum
// lines carry the appended state dump with a second view= key).
//
// Lines marked HAND-SHAPED never appear in an INFO-level log of a healthy local chain, so they
// are written in the shape of the code's LOG_KV calls: the DEBUG per-tx events (TxAdmitted /
// TxSealed / TxRemoved from MemoryStorage.cpp, TxExecuted from BaselineScheduler-tpp.h),
// HandshakeFailed (libnetwork/Common.cpp) and the tx-sync round (MemoryStorage.cpp
// ProposalTxsMissing, TransactionSync.cpp TxsRequested / TxsReceived).
inline std::string const c_eventsLog =
    R"(info|2026-10-10 02:11:16.056000|Unnamed-0x00000001f54a9e80|[CONSENSUS][PBFT]compatibilityVersion updated,version=3.18.0,updatedVersion=3.16.0
info|2026-10-10 02:11:16.061790|io-0x000000016c36b000|[P2PService][Service]PeerConnected,peer=cc77445e,endpoint=127.0.0.1:30501,direction=out,shortP2pid=hash-6db17e78
info|2026-10-10 02:11:50.066771|p2pTeardown-0x000000016c483000|[P2PService][Service]PeerDisconnected,peer=cc77445e,endpoint=127.0.0.1:30501,reason=remote_close,code=3,detail=remote_close
info|2026-10-10 02:12:07.549920|io-0x000000016d4bf000|[BLOCK SYNC]SyncStarted,number=14,highest=16,lag=2,peers=4
info|2026-10-10 02:12:07.553398|io-0x000000016d3a7000|[BLOCK SYNC][METRIC][Download]BlockApplied,number=15,hash=a1b4c5e2...,signatureSize=3,txsSize=1,nextBlock=15,executedBlock=15,timeCost=2,node=d03590e1...,sysBlock=false
info|2026-10-10 02:12:07.554064|io-0x000000016d433000|[BLOCK SYNC][METRIC][Download]BlockApplied,number=16,hash=d3ebb1d9...,signatureSize=3,txsSize=1,nextBlock=15,executedBlock=16,timeCost=1,node=d03590e1...,sysBlock=false
info|2026-10-10 02:12:07.554089|io-0x000000016d4bf000|[BLOCK SYNC]SyncFinished,number=14,costMs=4,blocks=0,reason=finished
info|2026-10-10 02:25:13.840184|io-0x000000016b833000|[NETWORK][Host]PeerConnectFailed,endpoint=127.0.0.1:30502,reason=tcp_connect,detail=Connection refused,consecutiveFailures=1
info|2026-10-10 02:25:13.843404|io-0x000000016b7a7000|[P2PService][Service]PeerConnected,peer=cc77445e,endpoint=127.0.0.1:30501,direction=out,shortP2pid=hash-6db17e78
warning|2026-10-10 02:25:13.900000|io-0x000000016b833000|[NETWORK][Host]HandshakeFailed,endpoint=127.0.0.1:30303,reason=ssl_handshake,detail=certificate verify failed
info|2026-10-10 02:25:31.847685|io-0x000000016b7a7000|[CONSENSUS][PBFT]ViewChangeTriggered,reason=consensus_timeout,waitingIndex=17,waitedMs=3000,view=0,toView=3,changeCycle=1,leaderIdx=1
info|2026-10-10 02:25:31.847952|io-0x000000016b7a7000|[CONSENSUS][PBFT]ViewChangeSent,view=0,toView=3,index=16,packetSize=164
info|2026-10-10 02:25:31.847973|io-0x000000016b7a7000|[CONSENSUS][PBFT]ViewChangeReceived,toView=3,fromIdx=1,weight=1,maxCommittedIndex=16,maxPrecommitIndex=0 preparedProposalInfo: ,committedIndex=16,consNum=17,committedHash=d3ebb1d9...,view=0,toView=3,changeCycle=1,expectedCheckPoint=17,Idx=1,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=b862ced9...
info|2026-10-10 02:25:31.852867|io-0x0000000170217000|[CONSENSUS][PBFT]ViewChangeQuorum,toView=3,weight=3,minRequiredQuorum=3
debug|2026-10-10 02:25:31.853000|io-0x000000017018b000|[TXPOOL]TxAdmitted,tx=9c0ffee0...,from=0x5b4a0e3c9d2f1a8b7c6d5e4f3a2b1c0d9e8f7a6b,nonce=12,blockLimit=519
info|2026-10-10 02:25:31.853374|io-0x000000017018b000|[TXPOOL][METRIC]TxsFetched,time=0,txsSize=1,sysTxsSize=0,pendingTxs=0,limit=1000,fetchTxsT=0,lockT=0,invalidBefore=0,sealed=0,traverseCount=1
debug|2026-10-10 02:25:31.853380|io-0x000000017018b000|[TXPOOL]TxSealed,tx=9c0ffee0...,batchId=19,batchHash=124891bd...
info|2026-10-10 02:25:31.857147|io-0x000000016b7a7000|[CONSENSUS][PBFT]SealSkipped,reason=not_leader,index=17,until=-1
info|2026-10-10 02:25:31.857244|io-0x000000016b7a7000|[CONSENSUS][PBFT]NewViewReached,view=3,leaderIdx=0,committedIndex=16,changeCycle=0,lowWaterMark=17,highWaterMark=67
info|2026-10-10 02:25:32.875958|io-0x000000016b71b000|[CONSENSUS][PBFT]SealResumed,index=18
info|2026-10-10 02:25:33.497303|io-0x0000000170217000|[CONSENSUS][PBFT]PrePrepareReceived,index=19,hash=124891bd...,view=3,fromIdx=2,fromNewView=false
info|2026-10-10 02:25:33.499672|io-0x0000000170073000|[LEDGER]asyncPreStoreBlockTxs,reason=stored,number=19,txsSize=1,unStoredTxs=1,msg=success,code=0,timeCost=0
info|2026-10-10 02:25:33.501706|io-0x0000000170217000|[CONSENSUS][PBFT]PrepareQuorum,index=19,hash=124891bd...,view=3,weight=3,signatureSize=3,committedIndex=18,consNum=19,committedHash=45d022b1...,view=3,toView=3,changeCycle=0,expectedCheckPoint=19,Idx=0,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=a5011a8c...
info|2026-10-10 02:25:33.503330|io-0x0000000170217000|[CONSENSUS][PBFT]######## CommitQuorum,index=19,hash=124891bd...,sys=false,committedIndex=18,consNum=19,committedHash=45d022b1...,view=3,toView=3,changeCycle=0,expectedCheckPoint=19,Idx=0,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=a5011a8c...
debug|2026-10-10 02:25:33.505000|io-0x0000000170073000|[BASELINE_SCHEDULER]TxExecuted,tx=9c0ffee0...,number=19,status=0,gasUsed=28012
info|2026-10-10 02:25:33.506156|io-0x0000000170073000|[CONSENSUS][PBFT]CheckpointSent,index=19,hash=4b49ea15...
info|2026-10-10 02:25:33.506195|io-0x0000000170073000|[CONSENSUS][PBFT]ProposalExecuted,index=19,hash=4b49ea15...,proposalHash=124891bd...,execMs=3,committedIndex=18,consNum=19,committedHash=45d022b1...,view=3,toView=3,changeCycle=0,expectedCheckPoint=20,Idx=0,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=a5011a8c...
info|2026-10-10 02:25:33.510228|io-0x0000000170217000|[CONSENSUS][PBFT]CheckpointQuorum,index=19,hash=4b49ea15...,weight=3,committedIndex=18,consNum=19,committedHash=45d022b1...,view=3,toView=3,changeCycle=0,expectedCheckPoint=20,Idx=0,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=a5011a8c...
info|2026-10-10 02:25:33.511392|io-0x000000016ffe7000|[BASELINE_SCHEDULER][METRIC]BlockStat,number=19,txs=1,execMs=2,commitMs=1
info|2026-10-10 02:25:33.511408|io-0x000000016ffe7000|[CONSENSUS][PBFT][STORAGE][METRIC]BlockCommitted,index=19,hash=4b49ea15...,txs=1,commitMs=1,commitPerTx=1
info|2026-10-10 02:25:33.511417|io-0x000000016ffe7000|[LEDGER][METRIC]asyncPrewriteBlock,number=19,totalTxs=19,failedTxs=0,incTxs=1,incFailedTxs=0,writeReceiptsMs=-1,writeTxsMs=-1
info|2026-10-10 02:25:33.511764|io-0x00000001700ff000|[CONSENSUS][PBFT][METRIC]^^^^^^^^Report,sealer=2,txs=1,committedIndex=19,consNum=20,committedHash=4b49ea15...,view=3,toView=3,changeCycle=0,expectedCheckPoint=20,Idx=0,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=a5011a8c...,roundMs=12
info|2026-10-10 02:25:33.511779|io-0x00000001700ff000|[CONSENSUS][PBFT][METRIC]BlockStat,number=19,prePrepareRecv=1,prepareRecv=3,commitRecv=3,checkpointRecv=3,viewChangeRecv=0,rejected=0,bytesRecv=2730
info|2026-10-10 02:25:33.511844|io-0x00000001700ff000|[BLOCK SYNC][METRIC]BlockStat,number=19,downloaded=0,applied=0,requests=0,lag=0,peers=4
info|2026-10-10 02:25:33.512115|Unnamed-0x0000000171ebf000|[TXPOOL][METRIC]TxsRemoved,number=19,expectedSize=1,succCount=1,batchId=19,timecost=0,lockT=0,removeT=0,updateLedgerNonceT=0,updateWeb3NonceT=0,updateTxPoolNonceT=0
debug|2026-10-10 02:25:33.512120|Unnamed-0x0000000171ebf000|[TXPOOL]TxRemoved,tx=9c0ffee0...,number=19,reason=committed
info|2026-10-10 02:25:33.512141|Unnamed-0x0000000171ebf000|[TXPOOL][METRIC]BlockStat,number=19,pending=0,sealed=0,added=1,removed=1,expired=0,rejected=0,rejectNonce=0,rejectBlockLimit=0,rejectSignature=0,rejectDuplicate=0,rejectFull=0,rejectOther=0
info|2026-10-10 02:25:34.010000|io-0x0000000170217000|[TXPOOL]ProposalTxsMissing,number=20,hash=e3df6a7e...,missed=1,total=1,verifyT=1
info|2026-10-10 02:25:34.010500|io-0x0000000170217000|[SYNC]TxsRequested,number=20,hash=e3df6a7e...,peer=cc77445e,count=1
info|2026-10-10 02:25:34.016000|io-0x0000000170217000|[SYNC][METRIC]TxsReceived,number=20,hash=e3df6a7e...,peer=cc77445e,count=1,decodeT=1,importT=2,costMs=5
this line has no prefix and must be skipped
)";
}  // namespace bcos::ops::test
