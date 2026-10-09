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
// Sample lines in the shape the handbook (docs/ops/log-events.md) documents: one PBFT round for
// block 129 after a viewchange 3 -> 4, one tx through its six stages, one sync segment, one seal
// stall, two peers, one tx-sync round, plus a TXPOOL BlockStat and the startup version line.
inline std::string const c_eventsLog =
    R"(info|2026-10-09 14:00:00.100000|Unnamed-0x1|[CONSENSUS][PBFT]compatibilityVersion updated,version=3.18.0,updatedVersion=3.16.0
info|2026-10-09 14:02:11.031000|io-0x2|[CONSENSUS][PBFT]ViewChangeTriggered,reason=consensus_timeout,waitingIndex=129,waitedMs=3001,view=3,toView=4,changeCycle=1,leaderIdx=0
info|2026-10-09 14:02:11.032000|io-0x2|[CONSENSUS][PBFT]ViewChangeSent,view=3,toView=4,index=128,packetSize=160
info|2026-10-09 14:02:11.040000|io-0x2|[CONSENSUS][PBFT]ViewChangeReceived,toView=4,fromIdx=2,weight=2,maxCommittedIndex=128,maxPrecommitIndex=0
info|2026-10-09 14:02:11.050000|io-0x2|[CONSENSUS][PBFT]ViewChangeQuorum,toView=4,weight=3,minRequiredQuorum=3
info|2026-10-09 14:02:11.060000|io-0x2|[CONSENSUS][PBFT]NewViewReached,view=4,leaderIdx=1,committedIndex=128,changeCycle=0,lowWaterMark=128
info|2026-10-09 14:02:14.102000|io-0x2|[CONSENSUS][PBFT]PrePrepareReceived,index=129,hash=85691626...,view=4,fromIdx=1
info|2026-10-09 14:02:14.118000|io-0x2|[CONSENSUS][PBFT]PrepareQuorum,index=129,hash=85691626...,view=4,weight=3,signatureSize=3
debug|2026-10-09 14:02:14.119000|io-0x2|[CONSENSUS][PBFT]CommitSent,index=129,hash=85691626...
info|2026-10-09 14:02:14.131000|io-0x2|[CONSENSUS][PBFT]######## CommitQuorum,index=129,hash=85691626...
debug|2026-10-09 14:02:14.150000|io-0x3|[TXPOOL]TxAdmitted,tx=9c0ffee0...,from=0xabc,nonce=12,blockLimit=629
debug|2026-10-09 14:02:14.151000|io-0x3|[TXPOOL]TxSealed,tx=9c0ffee0...,batchId=129,batchHash=85691626...
debug|2026-10-09 14:02:14.180000|io-0x4|[SCHEDULER]TxExecuted,tx=9c0ffee0...,number=129,status=0,gasUsed=28012
info|2026-10-09 14:02:14.187000|io-0x2|[CONSENSUS][PBFT]ProposalExecuted,index=129,hash=85691626...,proposalHash=85691626...,execMs=55
info|2026-10-09 14:02:14.188000|io-0x2|[CONSENSUS][PBFT]CheckpointSent,index=129,hash=85691626...
info|2026-10-09 14:02:14.201000|io-0x2|[CONSENSUS][PBFT]CheckpointQuorum,index=129,hash=85691626...,weight=3
info|2026-10-09 14:02:14.209000|io-0x2|[CONSENSUS][PBFT][STORAGE]BlockCommitted,index=129,hash=85691626...,txs=12,commitMs=7,commitPerTx=0
debug|2026-10-09 14:02:14.209500|io-0x3|[TXPOOL]TxRemoved,tx=9c0ffee0...,number=129,reason=committed
info|2026-10-09 14:02:14.209800|io-0x3|[TXPOOL][METRIC]BlockStat,number=129,pending=3,sealed=12,added=12,removed=12,expired=0,rejected=1,rejectNonce=1,rejectBlockLimit=0,rejectSignature=0,rejectDuplicate=0,rejectFull=0,rejectOther=0
info|2026-10-09 14:02:14.210000|io-0x2|[CONSENSUS][PBFT][METRIC]^^^^^^^^Report,sealer=1,txs=12,committedIndex=129,consNum=130,committedHash=85691626...,view=4,toView=4,changeCycle=0,expectedCheckPoint=130,Idx=0,sealUntil=0,waitResealUntil=0,consensusTimeout=3000,nodeId=3a1f00aa...,roundMs=108
info|2026-10-09 14:02:20.000000|io-0x5|[BLOCK SYNC]SyncStarted,number=129,highest=140,lag=11,peers=3
info|2026-10-09 14:02:21.000000|io-0x5|[BLOCK SYNC]BlockApplied,number=130
info|2026-10-09 14:02:23.500000|io-0x5|[BLOCK SYNC]SyncFinished,number=140,costMs=3500,blocks=11,reason=finished
info|2026-10-09 14:02:30.000000|io-0x6|[CONSENSUS][SEALER]SealSkipped,reason=no_txs,index=141,until=0
info|2026-10-09 14:02:35.000000|io-0x6|[CONSENSUS][SEALER]SealResumed,index=141
info|2026-10-09 14:02:40.000000|io-0x7|[P2PService][Service]PeerConnected,peer=3a1f01bb,endpoint=127.0.0.1:30301,direction=out,shortP2pid=3a1f01bb
info|2026-10-09 14:02:41.000000|io-0x7|[P2PService][Service]PeerDisconnected,peer=3a1f02cc,endpoint=127.0.0.1:30302,reason=remote_close,code=0,detail=remote_close
warning|2026-10-09 14:02:42.000000|io-0x7|[NETWORK][Host]HandshakeFailed,endpoint=127.0.0.1:30303,reason=ssl_handshake,detail=certificate verify failed
info|2026-10-09 14:02:50.000000|io-0x3|[TXPOOL]ProposalTxsMissing,number=142,hash=77aa...,missed=2,total=10,verifyT=1
info|2026-10-09 14:02:50.001000|io-0x3|[SYNC]TxsRequested,number=142,hash=77aa...,peer=3a1f01bb,count=2
info|2026-10-09 14:02:50.020000|io-0x3|[SYNC][METRIC]TxsReceived,number=142,hash=77aa...,peer=3a1f01bb,count=2,decodeT=1,importT=2,costMs=19
this line has no prefix and must be skipped
)";
}  // namespace bcos::ops::test
