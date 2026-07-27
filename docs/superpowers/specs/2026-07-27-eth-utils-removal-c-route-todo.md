# eth/utils 移除(C 路线)TODO 清单

日期:2026-07-27
状态:**占位登记,未实施**。用户已选定 C 路线(bcos-codec RLP + 自研 MPT + 自研内存账本,
彻底去除 `bcos-evm/bcos-evm/eth/utils/` vendor 目录);本文档与源码内
`TODO(eth-utils-removal)` 注释互为索引。
前置认知:实施本清单将**作废**移植 PR #5361 的等价性证据链(124 用例/33 向量/3558 比对、
upstream-diff golden),并与 `bcos-evm-ref` 永久分叉——实施前须重建等价性验收方案。

## 1. 需新建的替代件

| 替代件 | 替代对象 | 说明 |
|---|---|---|
| 自研 MPT + 建根 | `mpt.{hpp,cpp}`、`mpt_hash.{hpp,cpp}` | 仓内无现成 MPT;secure-trie(key=keccak(addr/slot))、账户树+逐账户存储树,空树根 = EMPTY_MPT_HASH |
| RLP 适配 | `rlp.hpp`、`rlp_encode.{hpp,cpp}` | 基于 `bcos-codec/bcos-codec/rlp/RLPEncode.h`;回执/交易编码输出须与 op-geth 逐字节等价 |
| 自研内存账本 | `test_state.{hpp,cpp}`(`TestState`) | 实现 `evmone::state::StateView` + `apply_diff`;须满足 StateDiffSanitize.h 头注的 KEEP 契约(存在但空 ≠ nullopt) |
| 自研向量装载器 | `statetest.hpp` + `test/support/statetest_loader.cpp` + `utils.{hpp,cpp}` + `blob_schedule.{hpp,cpp}` | JSON(t8n 向量 pre/交易)→ 自研账本;nlohmann 仍限测试目标 |
| stdx 迁移 | `stdx/utility.hpp` | 内联进 `eth/state/` 或迁 `eth/state/stdx/`(vendored `state.cpp:6`、`precompiles.cpp:6` 相对引用它,见 §3) |

## 2. 源码内已插 TODO 的改点(库侧)

| 文件 | 改点 |
|---|---|
| `adapter/StateRootCompute.{h,cpp}` | mpt_hash→自研建根;TestState 形参→自研账本 |
| `adapter/StateDiffWriteback.h` | TestState→自研账本;三条写回契约原样保留 |
| `opstack/OpBlockSeal.{h,cpp}` | 三根建根迁自研 MPT + bcos-codec RLP;`op_storage_root`/`receipts_root_loop` 属 upstream-diff 追踪段 |
| `opstack/OpReceiptEncode.cpp` | RLP→bcos-codec;输出为建根输入,字节等价由 gate 判定 |
| `opstack/OpTransition.cpp` | rlp 替换 = 照抄面重写(追踪段 24-29/82-135/155-165),golden 全量重做 |
| `opstack/OpPredeploys.{h→}.cpp` | `seedOpPredeploys(TestState&)` 签名连同头文件与全部调用方改自研账本 |
| `bcos-evm/CMakeLists.txt` | 库源 6 个 utils `.cpp` 移除 + 告警抑制列表清理 |

## 3. 不宜插注释、仅在此登记的改点

- **vendored `eth/state/state.cpp:6`、`precompiles.cpp:6`**:`#include "../utils/stdx/utility.hpp"`
  相对引用——stdx 迁移后需改这两行(vendored 文件首次破例修改,须在 §4.6 偏离台账追加记录)。
- **`eth/utils/` 整目录删除** 与 **`test/support/statetest_loader.cpp` 删除**。
- **`bcos-evm/vcpkg.json` / 根 `vcpkg.json`**:装载器自研后 `nlohmann-json` 是否仍需保留
  (若自研装载器仍用 nlohmann 则保留,若换 jsoncpp/boost-json 则移除)。

## 4. 测试面改点(21 文件,统一迁移)

- 15 个直接 include `test_state.hpp` 的测试(Op7702/OpBlockExecute/OpBlockFinalize/
  OpBlockHarness/OpBlockSeal/OpDeposit/OpFeeParams/OpFloorGas/OpHost/OpPredeploys/
  OpStateDiffSanitize/OpT8nReplay/OpTransition/OpValidate/OpZeroDiff)→ 自研账本。
- `OpBlockSealTest`/`OpReceiptEncodeTest` 对 mpt/mpt_hash/rlp_encode 的单元判别 → 改判自研件
  (金值不变:同一批 op-geth 向量)。
- `OpT8nReplayTest.cpp:32-34` 的 statetest/rlp include → 自研装载器。
- `test/CMakeLists.txt`:loader 源、eth/utils include 路径删除(已插 TODO)。
- **"测试代码零改写"原则就此终止**,测试等价性重验以 33/33 向量 + `known_diverges=0` 为准。

## 5. 护栏与文档连带

- **upstream-diff 护栏整体退役(2026-07-27 用户裁定"方式一":随照抄段重写自然退役,
  不做 golden 重做)**。照抄段重写完成(§6 第 4 步)即执行:
  - 删除 `bcos-evm/scripts/upstream-diff.sh` 与 `bcos-evm/scripts/upstream-diff/`
    全部内容(EVMONE_REF/manifest.tsv/normalize.sed/golden×10);`scripts/` 若因此
    为空则一并删除;
  - `OpTransition.cpp`/`OpBlockSeal.cpp` 内引用 manifest/追踪段/--regenerate-goldens
    的 TODO 注释去悬空指向;
  - `bcos-evm/README.md` 删护栏行;port spec §6.2 标注"已退役,转历史记录"、
    §8 验收清单该项标注退役、§4.6 追加条目(e)明示"照抄面静态追溯自此不存在,
    照抄段改动此后仅由 33 向量 gate 行为判定";
  - §4.6(c) 的"ref 侧 golden 欠账"跟进项在本仓侧随之关闭(ref 仓自身护栏不受影响)。
  - **退役前窗口期护栏保持有效**:凡触碰照抄段的中间步骤仍须跑护栏并按仪式处理
    (行号平移 / 有意改动 regenerate)。
- spec `2026-07-24-...-port-design.md`:§3 架构图、§4.2 vendor 清单、§4.6 偏离台账追加。
- `bcos-evm/README.md`:vendored 描述、目录清单更新。
- 与 `bcos-evm-ref` 的同步关系:C 路线后 ref→bcos-evm 的文件级同步不再可行,
  需改为"行为级对齐 + 向量 gate 判定"。

## 6. 建议实施顺序(未排期)

1. 自研账本(替 TestState)+ 测试迁移(gate 保绿)
2. RLP 适配层(OpReceiptEncode 先行,gate 判字节等价)
3. 自研 MPT + 三根建根(stateRoot 单腿比对族判定)
4. OpTransition 照抄段重写 + upstream-diff 护栏退役(方式一,见 §5 首条;不做 golden 重做)
5. 装载器自研 + eth/utils 目录删除 + 文档收尾
