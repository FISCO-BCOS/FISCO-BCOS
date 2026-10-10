# 节点日志事件手册

适用：`release-3.18.0` 日志整理后的节点（PR `feat/log-cleanup`）。整理前的日志文本见文末对照表。

## 行格式

```
info|2026-10-09 14:02:11.031|io-0x16e293000|[CONSENSUS][PBFT]ViewChangeTriggered,reason=consensus_timeout,waitingIndex=129,waitedMs=3001,view=3,toView=4,changeCycle=1,leaderIdx=0
```

四段用 `|` 分隔：级别、时间、线程、消息。消息 = 连续的 `[badge]` + 事件名 + `,k=v` 序列。事件名是 badge 后第一个词，全仓唯一（`tools/.ci/check_log_events.sh` 在 CI 里对本手册每个事件名核 `LOG_DESC` 恰好一处）。三个装饰符保留在事件名前面：`^^^^^^^^Report`、`++++++++++++++++ PrePrepareSent`、`######## CommitQuorum`；`[blk-N]` 与 `[METRIC]` 这两个 badge 也原样保留。

`grep <事件名> log/log_*.log` 就是精确查询。

## 级别规则

一行一秒最多打几次？上界是**块**或**状态变化** → INFO；上界是**交易**或**消息** → DEBUG。WARNING 及以上只给异常，每笔交易的失败不是 WARNING（走 DEBUG 一行 + `BlockStat` 按原因计数）。

## channel 名与运行时切级别

| channel | badge 文本 | 宏 |
|---|---|---|
| PBFT | `[CONSENSUS][PBFT]`、`[CONSENSUS][PBFT][STORAGE]`、`[CONSENSUS][Core]`、`[CONSENSUS][SEALER]` | `PBFT_LOG`、`PBFT_STORAGE_LOG`、`CONSENSUS_LOG`、`SEAL_LOG` |
| TXPOOL | `[TXPOOL]`、`[SYNC]`（交易同步）、`[TXPOOL][NonceChecker]`、`[TXVALIDATOR]` | `TXPOOL_LOG`、`SYNC_LOG`、`NONCECHECKER_LOG`、`TX_VALIDATOR_LOG` |
| SYNC | `[BLOCK SYNC]`、`[SYNCTREE]` | `BLKSYNC_LOG`、`SYNCTREE_LOG` |
| SCHEDULER | `[SCHEDULER]` 及派生 | `SCHEDULER_LOG`、`SCHEDULER_BLK_LOG`、`DMC_LOG` |
| EXECUTOR | `[EXECUTOR]` 及派生 | `EXECUTOR_LOG`、`EXECUTIVE_LOG`、`PARA_LOG`、`PRECOMPILED_LOG` |
| LEDGER | `[LEDGER]`、`[LEDGER2]`、`[MPT_PRUNER]` | `LEDGER_LOG`、`LEDGER2_LOG`、`MPT_PRUNER_LOG` |
| RPC | `[RPC]`、`[RPC][JSONRPC]`、`[RPC][WEB3]`、`[EVENT]`、`[FILTER]` | `RPC_LOG`、`RPC_IMPL_LOG`、`WEB3_LOG`、`EVENT_*`、`FILTER_LOG` |
| GATEWAY | `[Gateway][*]`、`[P2PService][*]`、`[NETWORK]`、`[SESSION]`、`[AMOP]` | `GATEWAY_LOG` 等全部网关宏 |
| FRONT | `[FrontService]` | `FRONT_LOG` |

每个 channel 有自己的级别表项，缺省继承 `[log] level`；本 PR 提供的是 `bcos-utilities/BoostLog.h` 的 `setModuleLogLevel` / `resetModuleLogLevel` / `moduleLogLevels` 这层 API。运行时改一个 channel 而不动文件的命令由后续 PR（运维工具 `fisco-bcos log-level`，经节点目录下的本机 socket 调 `admin_setLogLevel`）提供：

```
./fisco-bcos log-level set --module TXPOOL debug     # 后续 PR：只把交易池打到 DEBUG
./fisco-bcos log-level set --module TXPOOL inherit   # 后续 PR：回到跟随全局
./fisco-bcos log-level get                           # 后续 PR
```

`SIGUSR2` 重读 `[log] level` 的行为不变，只改全局项。

<!-- events -->

## 线索：PBFT 一轮

真实顺序：prePrepare → prepare 法定数 → commit 法定数 → **执行** → checkpoint 法定数 → 落账本 → Report。INFO 级别下每块 8 行左右。

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `PrePrepareSent` | `[CONSENSUS][PBFT]`，前缀 `++++++++++++++++ ` | INFO | index,hash,view,Idx,txsSize,packetSize,encodeMs | leader 本地处理并广播 prePrepare |
| `PrePrepareReceived` | `[CONSENSUS][PBFT]` | INFO | index,hash,view,fromIdx | follower 首次收到 prePrepare（校验后重入不再打） |
| `PrePrepareRejected` | `[CONSENSUS][PBFT]` | 按原因 INFO/WARNING/TRACE | index,hash,view,fromIdx,reason | reason：syncing、check_failed、too_large_view、non_local_view、not_leader、signature、index_mismatch、no_header、hash_mismatch、txs_root_mismatch、timestamp、in_flight_cap |
| `PrepareSent` | `[CONSENSUS][PBFT]` | DEBUG | index,packetSize | 广播 prepare |
| `PrepareReceived` | `[CONSENSUS][PBFT]` | DEBUG | index,view,fromIdx,weight | 每条 prepare 消息一行 |
| `PrepareQuorum` | `[CONSENSUS][PBFT]` | INFO | index,hash,view,weight,signatureSize | prepare 法定数达成，进入 precommit |
| `CommitSent` | `[CONSENSUS][PBFT]` | DEBUG | index,hash | 广播 commit |
| `CommitReceived` | `[CONSENSUS][PBFT]` | DEBUG | index,view,fromIdx,weight | 每条 commit 消息一行 |
| `CommitQuorum` | `[CONSENSUS][PBFT]`，前缀 `######## ` | INFO | index,hash,sys | commit 法定数达成，提案进入执行队列 |
| `ProposalExecuted` | `[CONSENSUS][PBFT]` | INFO | index,hash,proposalHash,execMs | 状态机执行完成 |
| `ProposalExecuteFailed` | `[CONSENSUS][Core]` | WARNING | index,hash,code,msg | 执行失败 |
| `CheckpointSent` | `[CONSENSUS][PBFT]` | INFO | index,hash | 执行完成后广播 checkpoint |
| `CheckpointReceived` | `[CONSENSUS][PBFT]` | DEBUG | index,fromIdx,weight,minRequiredWeight | 每条 checkpoint 消息一行 |
| `CheckpointQuorum` | `[CONSENSUS][PBFT]` | INFO | index,hash,weight | checkpoint 法定数达成，提交账本 |
| `PBFT:BlockCommitted` | `[CONSENSUS][PBFT][STORAGE]` | INFO | index,hash,txs,commitMs,commitPerTx | 块落账本 |
| `Report` | `[CONSENSUS][PBFT][METRIC]`，前缀 `^^^^^^^^` | INFO | sealer,txs,committedIndex,consNum,committedHash,view,toView,changeCycle,expectedCheckPoint,Idx,sealUntil,…,roundMs | 一轮结束；roundMs = prePrepare 入缓存到 committed 的耗时，取不到为 -1。**行为变化**：整理前同步来的块不打 `sealer=`/`txs=`，现在所有块都打，同步来的块为 `sealer=-1,txs=-1`（脚本按 `sealer=[0-9]` 过滤即可保留旧口径） |

```
n=129; grep -h "index=$n," log/log_*.log | grep '\[PBFT\]'
```

## 线索：viewchange

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `ViewChangeTriggered` | `[CONSENSUS][PBFT]` | INFO | reason,waitingIndex,waitedMs,view,toView,changeCycle,leaderIdx | 本节点发起 viewchange。reason：consensus_timeout、f_plus_one_higher_view、faulty_leader、startup_recovery、restart；waitedMs 只在 consensus_timeout 有值 |
| `ViewChangeSent` | `[CONSENSUS][PBFT]` | INFO | view,toView,index,packetSize | 广播 viewchange |
| `ViewChangeReceived` | `[CONSENSUS][PBFT]` | INFO | toView,fromIdx,weight,maxCommittedIndex,maxPrecommitIndex | 收到并接受一条 viewchange（上界 = 节点数 × 次数） |
| `ViewChangeRejected` | `[CONSENSUS][PBFT]` | 按原因 INFO/WARNING/DEBUG | toView,fromIdx,reason | reason：stale_index、stale_view、committed_conflict、prepared_view_invalid、prepared_proposal_invalid、signature |
| `ViewChangeQuorum` | `[CONSENSUS][PBFT]` | INFO | toView,weight,minRequiredQuorum | 新 leader 收齐法定数 |
| `NewViewSent` | `[CONSENSUS][PBFT]` | INFO | view,prePrepareCount | 新 leader 广播 newView |
| `NewViewReceived` | `[CONSENSUS][PBFT]` | INFO | view,fromIdx | 收到 newView |
| `NewViewRejected` | `[CONSENSUS][PBFT]` | WARNING | view,fromIdx,reason | reason：stale_view、viewchange_invalid、insufficient_weight、conflicting_prepared、preprepare_hash_mismatch、preprepare_view_mismatch、signature |
| `NewViewReached` | `[CONSENSUS][PBFT]` | INFO | view,leaderIdx,committedIndex,changeCycle,lowWaterMark | 进入新 view |

```
grep -h 'ViewChangeTriggered' log/log_*.log | tail -5
```

## 线索：封块停滞

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `SealSkipped` | `[CONSENSUS][PBFT]` | INFO | reason,index,until | 只在 reason 变化时打一次（sealer 与 PBFT 两个模块共用一份状态，都经 `PBFT_LOG` 打）。reason：no_txs、already_committed、not_leader、wait_reseal、sys_proposal_pending、prev_executing |
| `SealResumed` | `[CONSENSUS][PBFT]` | INFO | index | 停滞原因清空，重新封块 |

`++++++++++++++++ Generate proposal`（`[CONSENSUS][SEALER]`，键 index,curNum,hash,sysTxs,txsSize,version）保留原文，每个提案一行。

```
grep -h 'SealSkipped\|SealResumed' log/log_*.log | tail -5
```

## 线索：checkpoint 超时重发

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `CheckpointResend` | `[CONSENSUS][PBFT]` | WARNING | index,hash,resendCount | 本节点重发 checkpoint，次数累计 |

## 线索：交易生命周期

每笔交易六个阶段，均 DEBUG：收到并准入（TXPOOL）、封装（TXPOOL）、执行（SCHEDULER）、移出（TXPOOL）；INFO 级别下只有块级汇总。查单笔交易前先把 TXPOOL（执行阶段还有 SCHEDULER）切到 DEBUG。执行阶段按执行器通道各有一行：`executor_version=1` 的旧调度器（`SchedulerImpl`/`BlockExecutive`，badge `[SCHEDULER]`）与 AIR 默认的 baseline 调度器（`transaction-scheduler`，badge `[BASELINE_SCHEDULER]`），两者都在 SCHEDULER channel 下。

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `TxAdmitted` | `[TXPOOL]` | DEBUG | tx,from,nonce,blockLimit | 校验通过入池 |
| `TxRejected` | `[TXPOOL]` | DEBUG | tx,reason | reason 为 `TransactionStatus` 名：NonceCheckFail、BlockLimitCheckFail、InvalidSignature、AlreadyInTxPool、TxPoolIsFull、… |
| `TxSealed` | `[TXPOOL]` | DEBUG | tx,batchId,batchHash | 被封进提案 |
| `TxSealSkipped` | `[TXPOOL]` | DEBUG | tx,reason,blockLimit,nonce | 封装时跳过，reason：nonce、blocklimit |
| `TxExecuted` | `[SCHEDULER]` | DEBUG | tx,number,status,gasUsed | 回执生成（旧调度器 `BlockExecutive`，executor_version=1） |
| `BASELINE:TxExecuted` | `[BASELINE_SCHEDULER]` | DEBUG | tx,number,status,gasUsed | 回执生成（baseline 调度器，AIR 默认） |
| `TxRemoved` | `[TXPOOL]` | DEBUG | tx,number,reason | reason：committed、expired |
| `TxsFetched` | `[TXPOOL]` | INFO | 现有键 | 一次封装取走的交易批 |
| `TxsRemoved` | `[TXPOOL][METRIC]` | INFO | 现有键,number | 块提交后批量移出 |
| `TxsExpired` | `[TXPOOL]` | INFO | 现有键 | 定时清理过期交易 |
| `TxPoolFull` | `[TXPOOL]` | INFO | pending,limit | 池满，状态翻转时一次 |
| `TxPoolRecovered` | `[TXPOOL]` | INFO | pending,limit | 池恢复，状态翻转时一次 |
| `TxBroadcastFallback` | `[TXPOOL]` | INFO | reason,msg | 树路由不可用退回泛洪，进程内一次 |

executor 侧每笔交易的 revert 行（`[EXECUTOR]Revert transaction: …`、`EVMC_*`）全部 DEBUG，键里带 number、contextID、seq；和 `TxExecuted` 用 number 对上。

```
h=0x9c…; grep -h "tx=$h" log/log_*.log
```

## 线索：交易同步（follower 校验提案缺交易）

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `ProposalVerified` | `[TXPOOL]` | INFO | 现有键 | 提案校验完成 |
| `ProposalTxsMissing` | `[TXPOOL]` | INFO | number,hash,missed,total,verifyT | 提案里有本地没有的交易（missed=0 时为 DEBUG 的 batchVerifyProposal 行） |
| `TxsRequested` | `[SYNC]` | INFO | number,hash,peer,count | 向 leader 索取缺失交易 |
| `TxsReceived` | `[SYNC][METRIC]` | INFO | number,hash,peer,count,decodeT,importT,costMs | 取到并导入 |
| `TxsRequestFailed` | `[SYNC]` | INFO | number,hash,peer,count,code,msg | 索取失败 |

```
grep -h 'ProposalTxsMissing\|TxsRequested\|TxsReceived\|TxsRequestFailed' log/log_*.log | tail -8
```

## 线索：块同步

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `SyncStarted` | `[BLOCK SYNC]` | INFO | number,highest,lag,peers | 从空闲进入下载 |
| `BlockRequested` | `[BLOCK SYNC]` | DEBUG | from,to,peer | 发出一批块请求 |
| `BlockApplied` | `[BLOCK SYNC]` | INFO | 现有键 | 下载块执行完成 |
| `SYNC:BlockCommitted` | `[BLOCK SYNC]` | INFO | 现有键 | 下载块落账本 |
| `SyncFinished` | `[BLOCK SYNC]` | INFO | number,costMs,blocks,reason | 回到空闲，reason：finished、timeout |
| `PeerStatus` | `[BLOCK SYNC]` | DEBUG | peer,number,hash | 收到 peer 状态包 |

```
grep -h 'SyncStarted\|SyncFinished' log/log_*.log | tail -4
```

## 线索：P2P 连接

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `PeerConnected` | `[P2PService][Service]` | INFO | peer,endpoint,direction,shortP2pid | 握手成功，direction：in、out |
| `PeerDisconnected` | `[P2PService][Service]` | INFO | peer,endpoint,reason,code,detail | 会话关闭，reason：remote_close、local_close、timeout、handshake_failed、blacklist、error（重复会话在 `Service::onDisconnect` 里直接返回，不打这一行） |
| `HandshakeFailed` | `[NETWORK][Host]` | 入站 INFO / 出站与协议不匹配 WARNING | endpoint,reason,detail | reason：ssl_handshake、no_node_id（入站：任何能连到 P2P 端口的人都能触发，所以只到 INFO）、protocol_mismatch |
| `PeerConnectFailed` | `[NETWORK][Host]` | INFO | endpoint,reason,detail,consecutiveFailures | 主动连接失败；同一 endpoint 首次与每第 10 次 |

```
grep -h 'PeerConnected\|PeerDisconnected\|HandshakeFailed' log/log_*.log | tail -8
```

## 账本

| 事件 | badge | 级别 | 键 | 含义 |
|---|---|---|---|---|
| `asyncPrewriteBlock` | `[LEDGER][METRIC]` | INFO | number,totalTxs,failedTxs,incTxs,incFailedTxs,writeReceiptsMs,writeTxsMs | 块写入账本；非写交易路径两个 Ms 为 -1 |
| `asyncPreStoreBlockTxs` | `[LEDGER]` | INFO | reason,number,txsSize,unStoredTxs,msg,code,timeCost | reason：empty、no_unstored、stored |

## BlockStat（按块一行）

`[log] enable_block_stat=true` 时各模块在自己收到「第 N 块已提交」的位置各打一行 `[MODULE][METRIC]BlockStat,number=N,…`；默认关闭，关闭时计数器不加。不出块不打。

| 事件 | badge | 键 |
|---|---|---|
| `PBFT:BlockStat` | `[CONSENSUS][PBFT][METRIC]` | number,prePrepareRecv,prepareRecv,commitRecv,checkpointRecv,viewChangeRecv,rejected,bytesRecv |
| `TXPOOL:BlockStat` | `[TXPOOL][METRIC]` | number,pending,sealed,added,removed,expired,rejected,rejectNonce,rejectBlockLimit,rejectSignature,rejectDuplicate,rejectFull,rejectOther |
| `SYNC:BlockStat` | `[BLOCK SYNC][METRIC]` | number,downloaded,applied,requests,lag,peers |
| `SCHEDULER:BlockStat` | `[SCHEDULER][METRIC]` | number,txs,failed,execMs,commitMs,execPerTxUs（旧调度器，executor_version=1） |
| `BASELINE:BlockStat` | `[BASELINE_SCHEDULER][METRIC]` | number,txs,execMs,commitMs（baseline 调度器，AIR 默认） |

```
grep -h 'BlockStat,number=129,' log/log_*.log
```

<!-- /events -->

## 仓内消费者清单

改事件名或键之前先查这张表；这些脚本随本 PR 一起改过，改名要再牵动它们。

| 消费者 | 依赖的行 / 键 |
|---|---|
| `tools/BcosAirBuilder/build_chain.sh` `generate_mtail_scripts`（随每条链发布的 Prometheus 采集规则） | `[CONSENSUS][PBFT]ProposalExecuted,…,execMs=`（块执行耗时）；`[CONSENSUS][PBFT][STORAGE]BlockCommitted,…,commitMs=`（块提交耗时）；`[LEDGER][METRIC]asyncPrewriteBlock,number=`（块高）；`[TXPOOL]TxsFetched,…,pendingTxs=`（交易池待处理数）；`p2p_session_actived` 等网关行未改 |
| `tools/summary.sh` | `PrePrepareReceived,index=N,`（起始行）；`Report.*committedIndex=N,`（结束行）；`TxsRemoved,…,timecost=`；`ProposalExecuted,…,execMs=`；`Report,sealer=[0-9]`（只统计本节点共识出的块，`sealer=-1` 是同步来的）、其 `txs=`/`committedIndex=`/`consNum=`/`view=` 列序；`Generate proposal`；`ExecuteBlock request.*waitT`、`CommitBlock success`、`GetTableHashes success`（旧调度器行，baseline 通道下为空） |
| `tools/log_extract.sh` | `[blk-N]ExecuteBlock request`、`ExecuteBlock success`（旧调度器行，未改名） |
| `tools/.ci/ci_check_air.sh`、`ci_check_pro.sh`、`ci_check_baseline.sh` `check_consensus` | `NewViewReached`（进入新 view；启动恢复路径仍是 `checkAndTryToRecoverView: reachNewView`，脚本两者都认） |
| 保留原文、未改名的行 | `ExecuteBlock request`、`ExecuteBlock success`、`CommitBlock success`、`Notify block result success`（`[SCHEDULER]`）、`++++++++++++++++ Generate proposal`（`[CONSENSUS][SEALER]`）、`^^^^^^^^Report`、`Execute block` / `Execute block finished` / `Commit block finished`（`[BASELINE_SCHEDULER]`） |

## 旧文本 → 新事件名

| 旧文本（位置） | 处理 |
|---|---|
| `++++++++++++++++ Generating seal on` + `broadcast pre-prepare packet`（PBFTEngine） | 合一 → `++++++++++++++++ PrePrepareSent` |
| `handlePrePrepareMsg`（每块两次） | → `PrePrepareReceived`，只在首次入口 |
| `handlePrePrepareMsg: reject …`、`checkPrePrepareMsg failed`、FIB-130/142/126 各拒绝行 | 合一 → `PrePrepareRejected,reason=` |
| `broadcast prepare packet` | → `PrepareSent` DEBUG |
| `addPrepareCache` / `addCommitCache`（PBFTCache.h，每消息 INFO） | → `PrepareReceived` / `CommitReceived` DEBUG |
| `intoPrecommit` + `setSignatureList` | → `PrepareQuorum`；setSignatureList 行删 |
| `checkAndCommit`（PBFTCache） | 删 |
| `######## CommitProposal` | → `######## CommitQuorum` |
| `applyStateMachine finished`、`onProposalApplySuccess`、StateMachine 成功行 | 保留 `ProposalExecuted` 一处；其余删或 DEBUG |
| `proposal execute failed`（PBFTEngine）+ StateMachine 失败行 | 合一 → `ProposalExecuteFailed` WARNING |
| （checkpoint 发送无日志） | 新增 `CheckpointSent` |
| `handleCheckPointMsg: try to add …` + `addCheckPointMsg` | 保留一处 → `CheckpointReceived` DEBUG |
| `checkAndCommitStableCheckPoint` | → `CheckpointQuorum` |
| `resend checkpoint …`（PBFTCache.cpp 开头） | → `CheckpointResend`，加 resendCount |
| `commitStableCheckPoint success`（LedgerStorage） | → `BlockCommitted` |
| `After onTimeout`、`tryTriggerFastViewChange for the faulty leader`、f+1 触发行 | 删 → 新增 `ViewChangeTriggered,reason=` |
| `broadcastViewChangeReq` | → `ViewChangeSent` |
| `addViewChangeReq` | → `ViewChangeReceived` |
| `InvalidViewChangeReq: …` 各行 | 合一 → `ViewChangeRejected,reason=` |
| （viewchange 法定数无日志） | 新增 `ViewChangeQuorum` |
| `checkAndTryIntoNewView` 广播行 | → `NewViewSent` |
| `handleNewViewMsg: receive newViewChangeMsg` + `handleNewViewMsg success` | 合一 → `NewViewReceived` |
| `InvalidNewViewMsg …` 各行 | 合一 → `NewViewRejected,reason=` |
| `reachNewView` | → `NewViewReached`；`resetNewViewState`、`resetCacheAfterViewChange`、`resetCache*` → DEBUG |
| `notify to seal next proposal`（每次 tryToApplyCommitQueue）、sealer 无交易/已提交行、notifySealer 非 leader 行 | 删 → `SealSkipped,reason=` 只在原因变化时打 |
| `Submit transaction failed!`、sendTransaction error（JsonRpcImpl_2_0） | 删 → `TxRejected` DEBUG |
| `#if FISCO_DEBUG` 的封装行 | → `TxSealed` DEBUG |
| 封装时每笔 WARNING（nonce / blocklimit） | → `TxSealSkipped` DEBUG |
| `batchRemove txs success` / `batchFetchTxs success` / `cleanUpExpiredTransactions` | → `TxsRemoved` / `TxsFetched` / `TxsExpired` |
| `falling back to flood broadcast`（每笔 INFO） | → `TxBroadcastFallback`，进程内一次 |
| `asyncVerifyBlock finished` | → `ProposalVerified` |
| `batchVerifyProposal`（有缺失时） | → `ProposalTxsMissing` |
| `requestMissedTxs and verify success` / `fetch missed txs failed` | → `TxsReceived` / `TxsRequestFailed`；新增 `TxsRequested` |
| `[Download][requestBlocks]`、`Request blocks` | → `SyncStarted`（翻转时一次）、`BlockRequested` DEBUG |
| `applyBlock success` / `commitBlockState success` | → `BlockApplied` / `BlockCommitted` |
| `DMCExecute for transaction finished`（两行） | 保留一行 |
| `notify block result success`（BlockExecutive） | 删，SchedulerImpl 的保留 |
| `ExecuteBlock start`、缓存命中行 | → DEBUG |
| `Revert transaction: …` / `EVMC_*`（EXECUTIVE_LOG(INFO)） | → DEBUG，补 number/contextID/seq |
| `asyncPrewriteBlock` 两行 | 合一 |
| `asyncPreStoreBlockTxs: empty txs` / `no unstored txs` / `store uncommitted txs` | 合一 → `asyncPreStoreBlockTxs,reason=` |
| `Connection established` / `onDisconnect`（Service.cpp） | → `PeerConnected` / `PeerDisconnected` |
| ssl handshake / 协商失败各 WARNING | → `HandshakeFailed,reason=` |
| `clientConnect` 失败 ERROR、连接超时 WARNING | → `PeerConnectFailed`，按 endpoint 限频 |
