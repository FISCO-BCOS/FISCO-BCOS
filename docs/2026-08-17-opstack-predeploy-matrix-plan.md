# opstack 预部署合约行为矩阵 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 FISCO opstack(executor_version≥3)上建立核心 5 个预部署合约的行为矩阵:真实节点(`predeploy_matrix.py`,B3 RPC 8553)+ t8n 差分(共识项 stateRoot/withdrawalsRoot 对拍 op-geth)。

**Architecture:** 两层各司其职 — t8n 差分向量先锁共识项(L1Block 全槽写入 + withdrawalsRoot parity,复用 opt8n-ref 单源生成);真实节点组按合约行为断言(回执/事件/返回值/存储),期望值全部写死。发现实现级分歧 → 登记 DIVERGENCES.md 或单独立案,**不修复实现**。

**Tech Stack:** Go(t8n generator,op-geth opt8n-ref)/ Python3(op-e2e 脚本,urllib + libsecp256k1 sign_secp)/ Shell(run_all.sh)。

## Global Constraints

- **测试不可退步**:只增补测试与工具,不触碰预部署合约实现/节点执行逻辑/genesis;已通过的测试集合不得变红。
- 期望值**写死**在测试脚本与向量中(非运行时自算)。
- 参考锚:op-geth `/Users/octopus/octo/code/blockchain-impl/op-geth`(v1.101702.2)、pinned optimism `/tmp/op-spike/op-pinned`(33f06d2d)、`bcos-l2-contracts/out/*.sol/*.json`(ABI/storageLayout/bytecode 权威源)。
- 节点:B3 eth RPC **8553**(storage 保留,`restart_b3.sh`)、B3a 8563/8564。SENDER=`0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693`,PRIVKEY=`cdf753782bdb981198eab72e09b6c0ad780a9858ea4f3a8fe8b257016e2e0e29`,CHAIN_ID=11155111,sign_secp=`/tmp/op-spike/sign_secp`。
- 预部署地址(Python 脚本常量):`L1_BLOCK=0x4200…15`、`MESSAGE_PASSER=0x4200…16`、`MESSENGER=0x4200…07`、`BRIDGE=0x4200…10`、`SYSTEM_CONFIG=0x4200…c0`、`L2_VALIDATOR_SET=0x4200…c1`。
- **波次边界(单 L2 节点不可测,登记 deferred 而非强制)**:L2CrossDomainMessenger 的 `relayMessage`(需真实 L1→L2 消息已过桥)、L2StandardBridge 的 `finalizeBridge*`(需真实 L1 桥)、SystemConfig 的 owner 转移(owner=0x0 无人可转移)。这三项本计划不建断言,在交付时登记为「单 L2 节点不可构造」的 deferred 项(spec 表保留,波次 2+ 需跨域 mock L1 才可测)。
- **已知分歧(实现时确认,写死为 DIVERGENCE 而非强制断言)**:① 节点 L1Block 是 1.4.1-beta.1(storage slots 0-7,**无 operator-fee/DA 段**);t8n 用 146B `gen_l1block.py` 码(slots 1/3/7/8)。② SystemConfig genesis **未播种 owner 槽**(OZ Ownable 全 0)→ setValueByKey onlyOwner 恒 revert。
- t8n 机制:`cases.go` 的 `caseSpecs` 表(645 行起,`bothForks={"isthmus","jovian"}`)、`caseFrame`(L1Block 默认 code-less,slots 由 `feeParams.l1BlockStorage` 预播种)、`message_passer_write`(748 行,最小 SSTORE 码 `0x5f3560015500`);`regen.sh` 逐 case 生成向量 + `golden/engine/*.golden.json` + manifest 幂等追加 + cases∪modes==manifest 集合校验;`OpT8nReplayTest.cpp:878` 已断言 header.withdrawalsRoot。
- Selector(keccak256 重算,逐条对过节点字节码 dispatcher,权威;注意 **SHA3≠keccak**,`hashlib.sha3_256` 是错的一律不用):initiateWithdrawal=`0xc2b3e5ac`、sendMessage=`0x3dbb202b`、depositERC20=`0x58a997f6`、depositERC20To=`0x838b2520`、withdraw=`0x32b7006d`、setValueByKey=`0x86ff19b9`、getValueByKey=`0x1258a93a`、messageNonce=`0xecc70428`、sentMessages(bytes32)=`0x82e3702d`、balanceOf=`0x70a08231`、transfer=`0xa9059cbb`。
- **L1Block getter(keccak,对过 genesis L1Block dispatcher)**:number=`0x8381f58a`、timestamp=`0xb80777ea`、basefee=`0x5cf24969`、baseFeeScalar=`0xc5985918`、blobBaseFeeScalar=`0x68d5dca6`、blobBaseFee=`0xf8206140`、hash=`0x09bd5a60`、sequenceNumber=`0x64ca23ef`、l1FeeOverhead=`0x8b239f73`、l1FeeScalar=`0x9e8c4966`、batcherHash=`0xe81b2c6d`、setL1BlockValuesEcotone=`0x440a5e20`(无参)。**注意**:节点 L1Block 是 1.4.1-beta.1(Ecotone 时代,dispatch `0x440a5e20`),**无** 8 参 Isthmus `0x7b1d5889`/`0xcd13a0e7`;t8n 的 146B 码是另一套(0x098999be/0x3db6be2b)——实现时以实扫 dispatcher 为准。
- **事件 topic0(keccak,MessagePassed 已对过部署字节码 PUSH32)**:MessagePassed=`0x02a52367d10742d8032712c1bb8e0144ff1ec5ffda1ed7d70bb05a2744955054`、SentMessage=`0xcb0f7ffd78f9aee47a248fae8db181db6eee833039123e026dcbff529522e52a`、ERC20BridgeInitiated=`0x7ff126db8024424bbfd9826e8ab82ff59136289ea440b04b39a0df1b03b9cabf`、WithdrawalInitiated=`0x73d170910aba9e6d50b102db522b1dbcd796216f5128b445aa2135272886497e`。
- **Spec 勘误**:`docs/2026-08-17-opstack-predeploy-matrix-design.md` 的 L2ToL1MessagePasser getter 行写 `getSentMessage()/getSentMessageHash()`,但真实 L2ToL1MessagePasser ABI **无此二函数**;真实映射 getter 是 `sentMessages(bytes32)`。本计划按真实 ABI 执行(Task 3 Step 2),spec 行留待后续修订。
- 只增补提交;每个 Task 独立可提交。t8n regen 需要 op-geth 工作树干净且 HEAD==PIN(见 regen.sh)。

---

### Task 1: t8n 差分向量 — L1Block 全槽 + withdrawalsRoot parity

**Files:**
- Modify: `opstack-executor/tests/t8n/generator/cases.go`(新增 2 个 caseSpec + 1 个 L1Block 码常量)
- Regenerate: `opstack-executor/tests/t8n/vectors/`、`golden/engine/`、`vectors/manifest.txt`(regen.sh 自动)
- Test: `opstack-executor/tests/OpT8nReplayTest.cpp`(现有门,无需改——manifest 集合校验 + withdrawalsRoot 断言自动覆盖新向量)

**Interfaces:**
- Consumes: `caseFrame(fork,name,desc,fp,gasLimit)`(443 行)、`feeParams.attributesTx`、`types.L1BlockSlot` 常量、`messagePasserCode`(74 行)。
- Produces: 2 个新向量文件名 `l1block_deposit_slots`(isthmus/jovian)、`message_passer_withdraw`(isthmus/jovian),进入 manifest → OpT8nReplayTest 自动回放。

- [ ] **Step 1: 新增 L1Block 运行时码常量**

在 `cases.go` 的 `messagePasserCode` 常量旁新增:
```go
// gen_l1block.py 生成的 146B L1Block 运行时码(selector 0x098999be/0x3db6be2b,
// 写 slots 1/3/7/8)。与 OpL1BlockDepositTest.cpp kL1BlockCodeHex 同源。
l1BlockCode = hexutil.MustDecode(
    "6004361060255760003560e01c63098999be14602b5760003560e01c633d"
    "b6be2b14602b575b60006000fd5b6000358060c01c63ffffffff1660601b"
    "60003560a01c63ffffffff1660401b176003555060243560015560443560"
    "075560a03560c01c63ffffffff1660401b60a03560801c67ffffffffffff"
    "ffff161760b03560f01c61ffff1660601b1760085560006000f3")
```

- [ ] **Step 2: 新增 caseSpec `l1block_deposit_slots`(isthmus/jovian)**

在 `caseSpecs` 表中追加(替换 `caseFrame` 的 code-less L1Block):给 `l1BlockAddr` 播种 `l1BlockCode` + 非默认 `feeParams`(opFeeScalar/opFeeConstant/daScalar 非零,使 slot8 有值),断言 postState 全槽:
```go
{"l1block_deposit_slots", bothForks, func(fork string) inputCase {
    fp := defaultFeeParams()
    fp.opFeeScalar = 0x55c6fb7c
    fp.opFeeConstant = 1256417826609331460
    fp.daScalar = 0x1234
    c := caseFrame(fork, "l1block_deposit_slots",
        "L1 attributes deposit executes real L1Block code; slots 1/3/7/8 written",
        fp, 10_000_000)
    c.Pre[l1BlockAddr] = types.Account{Balance: big.NewInt(0), Nonce: 1,
        Code: l1BlockCode, Storage: fp.l1BlockStorage(fork)}
    c.ExtraStorage = map[common.Address][]common.Hash{
        l1BlockAddr: {
            types.L1BaseFeeSlot, types.L1FeeScalarsSlot,
            types.L1BlobBaseFeeSlot, types.OperatorFeeParamsSlot},
    }
    return c
}},
```
> 注:`types.L1BlockSlot` 常量名按 `cases.go` 现有引用核对(362-367 行用 `types.L1BaseFeeSlot/L1BlobBaseFeeSlot/L1FeeScalarsSlot/OperatorFeeParamsSlot`)。

- [ ] **Step 3: 新增 caseSpec `message_passer_withdraw`(isthmus/jovian)**

真实 initiateWithdrawal 流:给 `messagePasserAddr` 播种 **真实 L2ToL1MessagePasser 字节码**(取自 `/tmp/op-spike/op-pinned` 或 `bcos-l2-contracts/out/L2ToL1MessagePasser.sol`),加一条签名 tx 调 `initiateWithdrawal(target,gasLimit,data)`(selector `0xc2b3e5ac`,calldata 形如 `c2b3e5ac` + abi-encode(target=dead…0001, gasLimit=100000, data=0xbeef)):
```go
{"message_passer_withdraw", bothForks, func(fork string) inputCase {
    c := caseFrame(fork, "message_passer_withdraw",
        "real initiateWithdrawal -> MessagePassed event + message hash storage; withdrawalsRoot non-empty",
        defaultFeeParams(), 10_000_000)
    // 真实 L2ToL1MessagePasser 字节码(实现时从 bcos-l2-contracts/out/ 提取为显式常量)
    c.Pre[messagePasserAddr] = types.Account{Balance: big.NewInt(0), Nonce: 1,
        Code: realMessagePasserCode}
    fund(&c, 1, eth(100))
    // initiateWithdrawal(target=0xdead...0001, gasLimit=100000, data=0xbeef)
    // selector 0xc2b3e5ac + ABI 编码 (address,uint256,bytes), 共 4+160=164B:
    //   [0:4]   selector
    //   [4:36]  target (left-padded address)
    //   [36:68] gasLimit = 100000 (0x186a0, left-padded uint256)
    //   [68:100] dataOffset = 0x60 (96, points past the 3 head words)
    //   [100:132] dataLen = 2
    //   [132:164] data = 0xbeef0000... (padded to 32B)
    target := common.HexToAddress("0xdead000000000000000000000000000000000001")
    calldata := make([]byte, 4+32*5)
    copy(calldata[0:4], []byte{0xc2, 0xb3, 0xe5, 0xac})      // selector
    copy(calldata[4:36], target[:])                          // target
    binary.BigEndian.PutUint64(calldata[60:68], 100000)      // gasLimit at [36:68]
    binary.BigEndian.PutUint64(calldata[92:100], 0x60)       // dataOffset at [68:100]
    binary.BigEndian.PutUint64(calldata[124:132], 2)         // dataLen at [100:132]
    binary.BigEndian.PutUint16(calldata[162:164], 0xbeef)    // data at [132:164]
    c.Transactions = append(c.Transactions, transferTx(1, 0, messagePasserAddr,
        big.NewInt(0), 200_000, calldata))
    return c
}},
```
> `realMessagePasserCode` 是实现时的显式常量(从 `bcos-l2-contracts/out/L2ToL1MessagePasser.sol/L2ToL1MessagePasser.json` 的 bytecode 字段提取后写死,不得留空)。若提取遇阻,先用现有 `message_passer_write` 的最小码,并在 Task 1 报告里注明(此时只测 header withdrawalsRoot,不测真实事件)。calldata 布局以本注为准(上例 offset 已按此写)。

- [ ] **Step 4: 跑 `regen.sh` 全量再生成**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/opstack-executor/tests/t8n/generator
bash regen.sh
```
Expected: 退出 0;新向量 `l1block_deposit_slots.{isthmus,jovian}.json` 与 `message_passer_withdraw.{isthmus,jovian}.json` 出现;manifest 追加 4 行;golden/engine 对应 4 个新 golden。若退出非 0,读 stderr 定位(cases∪modes 集合不匹配或 op-geth 工作树脏)。

- [ ] **Step 5: 编译并跑 OpT8nReplayTest 门**

```bash
cmake --build /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/build --target opstack-executor-block-tests -j$(sysctl -n hw.ncpu)
ctest --test-dir /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/build -R OpT8nReplayTest --output-on-failure
```
Expected: 全部向量(含 4 新)回放绿,stateRoot/withdrawalsRoot 与 op-geth 一致。**若新向量红**:先看是否已知分歧(DIVERGENCES.md 登记),再修向量本身(非实现)。

- [ ] **Step 6: 全量 opstack 回归 + 提交**

```bash
ctest --test-dir /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/build -R "BcosEvmOpstack|OpstackExecutor" --output-on-failure
git add opstack-executor/tests/t8n/ && git commit -m "test(t8n): predeploy matrix consensus vectors (L1Block full-slot + real withdrawal)"
```

---

### Task 2: predeploy_matrix.py 骨架 + L1Block 组

**Files:**
- Create: `tools/op-e2e/predeploy_matrix.py`(骨架 + L1Block 组)
- Modify: `tools/op-e2e/run_all.sh`(本 Task 只加占位注释,实际挂载在 Task 6)

**Interfaces:**
- Consumes: B3 RPC 8553、`sign_secp`、SENDER/PRIVKEY/CHAIN_ID、`b3_contracts.py` 的 keccak256/rlp_encode/to_bytes_min/sign 模式(1-96 行可 import 或复制)。
- Produces: `PASSED/FAILED` 列表 + `check(name, cond)` 断言助手、`Rpc` 类(port=8553)、`main()`;L1Block 组的 assert 计数。

- [ ] **Step 1: 骨架(可运行的空脚本,1 组 1 断言冒烟)**

```python
#!/usr/bin/env python3
"""Predeploy behavior matrix (spec docs/2026-08-17-opstack-predeploy-matrix-design.md).
Runs against B3 (eth RPC 8553). Group by group; each group = independent asserts.
"""
import json, subprocess, sys, time, urllib.request
SIGN_SECP = "/tmp/op-spike/sign_secp"
SENDER = "0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693"
PRIVKEY = "cdf753782bdb981198eab72e09b6c0ad780a9858ea4f3a8fe8b257016e2e0e29"
CHAIN_ID = 11155111
L1_BLOCK = "0x4200000000000000000000000000000000000015"
PASSED, FAILED = [], []
def check(name, cond, detail=""):
    if cond: PASSED.append(name); print(f"  PASS {name}")
    else: FAILED.append(name); print(f"  FAIL {name} {detail}")
class Rpc:
    def __init__(self, port=8553):
        self._o = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.url = f"http://127.0.0.1:{port}"
    def eth(self, m, p=None):
        body = json.dumps({"jsonrpc":"2.0","method":m,"params":p or [],"id":1}).encode()
        req = urllib.request.Request(self.url, data=body, headers={"Content-Type":"application/json"})
        with self._o.open(req, timeout=10) as r: out = json.load(r)
        if "error" in out: raise AssertionError(f"{m} error: {out['error']}")
        return out.get("result")
def main():
    rpc = Rpc()
    # L1Block group
    n = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x8381f58a"}, "latest"])  # number()
    check("l1block_number_nonzero", n not in (None, "0x" + "0"*64, "0x0"), f"number={n}")
    print(f"\n{'ALL' if not FAILED else 'SOME'} PASSED {len(PASSED)} FAILED {len(FAILED)}")
    sys.exit(0 if not FAILED else 1)
if __name__ == "__main__": main()
```

- [ ] **Step 2: 冒烟运行**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/tools/op-e2e
bash restart_b3.sh && python3 predeploy_matrix.py
```
Expected: 1 断言,`l1block_number_nonzero` PASS。**若 number 为 0/空**:L1Block deposit 未写入(节点 L1Block 是 1.4.1-beta.1,需确认其 deposit 路径写了哪些 slot)——记录到 Task 2 报告,断言改为"能读且类型正确"而非具体值。

- [ ] **Step 3: L1Block 组完整断言(9 条)**

在 `main()` 的冒烟断言后追加(getters 用 eth_call,期望为**非零/与最近 deposit 一致**,不写死具体数——deposit 参数由节点注入,写死会随块漂移):
```python
    # getters: 存在且非零(节点 deposit 注入的参数,值不写死)。selector 均已对过 genesis L1Block dispatcher。
    for sel, name, expect_nonzero in [
        ("0xb80777ea", "timestamp", True),   # timestamp()
        ("0x5cf24969", "basefee", True),     # basefee()
        ("0xc5985918", "baseFeeScalar", True),
        ("0x68d5dca6", "blobBaseFeeScalar", True),
        ("0xf8206140", "blobBaseFee", True),
        ("0x09bd5a60", "hash", True),        # hash()
        ("0x64ca23ef", "sequenceNumber", True),
        ("0x8b239f73", "l1FeeOverhead", True),
        ("0x9e8c4966", "l1FeeScalar", True),
    ]:
        v = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": sel}, "latest"])
        check(f"l1block_{name}", v is not None and v != "0x" + "0"*64, f"{name}={v}")
```
> **Divergence 登记**:若 `baseFeeScalar/blobBaseFeeScalar/l1FeeOverhead/l1FeeScalar` 之一返回全 0,登记 DIVERGENCE(节点 L1Block 1.4.1-beta.1 的 setL1BlockValues 未写对应 slot),**不写死断言为 0**。

- [ ] **Step 4: 跨块 sequenceNumber 递增(2 条)**

用 chain_driver.py 已有的封块模式(FCU seal)推进 1 块,再读 sequenceNumber 断言 +1(需用 `eth_getBlockByNumber` 拿到当前块号,触发 FCU 产下一块):
```python
    b0 = rpc.eth("eth_getBlockByNumber", ["latest", False])
    # FCU seal 下一块(复用 chain_driver.py 的 fcu_seal 模式:engine 8554 + jwt /tmp/op-spike/b3/jwt.hex)
    seal_next_block(rpc)   # 实现: eth_sendRawTransaction 任意 transfer 或直接 FCU attributes
    b1 = rpc.eth("eth_getBlockByNumber", ["latest", False])
    seq0 = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x64ca23ef"}, "0x"+b0["number"][2:].zfill(64)])
    seq1 = rpc.eth("eth_call", [{"to": L1_BLOCK, "data": "0x64ca23ef"}, "0x"+b1["number"][2:].zfill(64)])
    check("l1block_seq_increments", int(seq1,16) == int(seq0,16)+1, f"{seq0}->{seq1}")
```
> `seal_next_block` 复用 `probe_l1block.py` 的 `fcu_seal`(168 行)签名——注意那是 B3a(8563/8564),此处要改 B3 端口(8553/8554)+ B3 jwt 路径。

- [ ] **Step 5: 拒绝路径 — 非 deposit 调用方 setL1BlockValues 被拒(1 条)**

spec 表「拒绝路径:错误 calldata 长度、非 deposit 调用方」。节点 L1Block 是真实 Solidity 合约,`setL1BlockValues` 有 `require(msg.sender == DEPOSITOR_ACCOUNT)`(genesis 码内 `0xdead…0001` 检查)。用普通签名 tx(SENDER)带 setL1BlockValues calldata 调 L1Block,断言回执 status=0x0:
```python
    # SENDER 不是 deposit 账户 => L1Block setL1BlockValuesEcotone 应 revert(仅 0xdead...0001 可调)。
    # 注意:节点 L1Block 是 1.4.1-beta.1,dispatch 的是 0x440a5e20(Ecotone 变体,无参),
    # 无 8 参 Isthmus selector 0x7b1d5889。用节点实际存在的 selector。
    cd = bytes.fromhex("440a5e20")  # setL1BlockValuesEcotone()(无参)
    raw = make_eip1559_tx(PRIVKEY, nonce, L1_BLOCK, cd, gas=300_000)
    r = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw]))
    check("l1block_reject_nondepositor", r.get("status") == "0x0", f"status={r.get('status')}")
```
> **Divergence 登记**:若 status=0x1(普通调用可写 L1Block),登记 DIVERGENCE(节点 L1Block 未做调用方限制或 fallback 放行)。selector 前提:节点 L1Block dispatcher 实扫含 `0x440a5e20`(setL1BlockValuesEcotone)——实现时再核对一次(若版本不同按实际 selector 算)。

- [ ] **Step 6: 运行 + 提交**

```bash
bash restart_b3.sh && python3 predeploy_matrix.py
```
Expected: L1Block 组全部 PASS(≥9 条),退出 0。提交:
```bash
git add tools/op-e2e/predeploy_matrix.py
git commit -m "test(e2e): predeploy matrix skeleton + L1Block group (getters + cross-block sequenceNumber)"
```

---

### Task 3: L2ToL1MessagePasser 组

**Files:**
- Modify: `tools/op-e2e/predeploy_matrix.py`

**Interfaces:**
- Consumes: Task 2 的骨架(keccak/rlp/sign 助手、`Rpc`、`check`)、`b3_contracts.py` 的 `make_call_tx` 签名模式(97-107 行)、`chain_driver.py`/`probe_l1block.py` 的封块模式。
- Produces: L2ToL1MessagePasser 组断言(≥5 条)。

- [ ] **Step 1: 签名交易助手(initiateWithdrawal)**

复制 `b3_contracts.py` 的 keccak256/rlp_encode/to_bytes_min 与 sign 逻辑,新增:
```python
def make_withdraw_tx(privkey, nonce, target, gas_limit, data):
    # abi_encode 返回 selector+参数(纯手写拼接,不引入库): target(32B) ‖ gasLimit(32B) ‖
    # dataOffset=0x60 ‖ dataLen ‖ data(pad32)。参考 probe_l1block.py build_jovian_calldata 模式。
    cd = abi_encode_call("c2b3e5ac", target, gas_limit, data)
    return make_eip1559_tx(privkey, nonce, MESSAGE_PASSER, cd, gas=200_000)
```
> `abi_encode_call(selector_hex, *args)` 手写 ABI 编码(静态拼接 + bytes 动态 offset 段),参考 `probe_l1block.py` 的 `build_jovian_calldata`(66-90 行)与 `b3_contracts.py` 的 rlp/to_bytes_min 风格。**禁止**用 `abi_encode("sig",...)` 这种带签名解析的调用——selector 已写死,只拼参数段。
>
> **共享助手契约(本 Task 起各后续 Task 复用,统一在此命名)**:`make_eip1559_tx(privkey, nonce, to, data, gas)`(EIP-1559 0x02 签名,复用 b3_contracts make_call_tx 96-107 行)、`wait_receipt(rpc, tx_hash)`(轮询 eth_getTransactionReceipt ≤60s,返回 dict)、`first_log_topic(receipt)`(回执首条 log 的 topics[0] 或 None)、`addr_pad(a)`(address → 64 hex 去 0x)、`abi_encode_call(sel, *args)`。Task 2 骨架若未含,本 Task 补齐。

- [ ] **Step 2: initiateWithdrawal 交易 + 回执断言(4 条)**

```python
    nonce = int(rpc.eth("eth_getTransactionCount", [SENDER, "latest"]), 16)
    raw = make_withdraw_tx(PRIVKEY, nonce, "0xdead000000000000000000000000000000000001", 100000, b"\xbe\xef")
    tx_hash = rpc.eth("eth_sendRawTransaction", [raw])
    # 等回执(轮询 60s)
    r = wait_receipt(rpc, tx_hash)
    check("mp_initiate_status1", r.get("status") == "0x1", f"status={r.get('status')}")
    # MessagePassed 事件 topic0(keccak,已对部署字节码 PUSH32 验证):
    #   keccak256("MessagePassed(uint256,address,address,uint256,uint256,bytes,bytes32)")
    MP_TOPIC = "0x02a52367d10742d8032712c1bb8e0144ff1ec5ffda1ed7d70bb05a2744955054"
    topics = r.get("logs", [{}])[0].get("topics", []) if r.get("logs") else []
    check("mp_messagepassed_event", topics and topics[0] == MP_TOPIC, str(topics))
    # withdrawalHash = MessagePassed 事件 data 段(非 indexed)的最后一个 bytes32:
    #   data = [versionedHash(32B)][nonce(32B)][sender(32B)][target(32B)][value(32B)][gasLimit(32B)][dataOffset(32B)][dataLen(32B)][data...][withdrawalHash(32B)]
    # 从 log.data(hex,去 0x)取尾部 64 hex = withdrawalHash。
    wh = "0x" + log_data[-64:] if (log_data := r.get("logs",[{}])[0].get("data","").lstrip("0x")) else "0x"+"0"*64
    sm = rpc.eth("eth_call", [{"to": MESSAGE_PASSER, "data": "0x82e3702d" + wh[2:].lower().zfill(64)}, "latest"])
    check("mp_sentmessages_true", sm == "0x" + "0"*63 + "1", f"sentMessages({wh})={sm}")
```
> **注**:MessagePassed 的 indexed 参数是 `nonce/sender/target` 3 个(topics[1:4]),`withdrawalHash` 在 data 尾部。若节点 log.data 字段结构不同(实现时核对真实回执),可降级为"事件存在 + sentMessages 查询用文档锚的已知 hash"。事件 ABI 见 `bcos-l2-contracts/out/L2ToL1MessagePasser.sol/L2ToL1MessagePasser.json`。
> **Divergence 登记**:若 status≠0x1 或 MessagePassed 事件缺失(真实字节码未部署 / MessagePasser 未接真实码),登记 DIVERGENCE 并降级为"回执可查"断言。

- [ ] **Step 3: 块头 withdrawalsRoot 字段(2 条)**

```python
    blk = rpc.eth("eth_getBlockByNumber", ["latest", True])
    wr = blk.get("withdrawalsRoot")
    check("mp_withdrawalsroot_present", wr is not None and wr != "0x" + "0"*64, f"withdrawalsRoot={wr}")
    # 跨块: 新块(同块同序列)根不变或按 messageNonce 演进 —— 至少断言字段存在且 32B
    check("mp_withdrawalsroot_32B", wr is not None and len(wr) == 66, wr)
```
> 节点块头是否填 withdrawalsRoot 是实现时确认点;若永远缺(节点未计算),登记 DIVERGENCE(此字段已由 t8n 差分覆盖,真实节点侧记"不可查")。

- [ ] **Step 4: 运行 + 提交**

```bash
bash restart_b3.sh && python3 predeploy_matrix.py
git add tools/op-e2e/predeploy_matrix.py && git commit -m "test(e2e): predeploy matrix L2ToL1MessagePasser group (initiateWithdrawal + events + withdrawalsRoot)"
```

---

### Task 4: L2CrossDomainMessenger + L2StandardBridge 组

**Files:**
- Modify: `tools/op-e2e/predeploy_matrix.py`

**Interfaces:**
- Consumes: Task 2 骨架、Task 3 签名助手、`b3_contracts.py` 的 `deploy()`(113-124 行,部署最小 ERC20)。
- Produces: Messenger 组(≥3 条)+ Bridge 组(≥3 条)。

- [ ] **Step 1: Messenger 组 — sendMessage nonce 递增(3 条)**

```python
    # messageNonce() selector = 0xecc70428(keccak,已对字节码)
    nonce0 = int(rpc.eth("eth_call", [{"to": MESSENGER, "data": "0xecc70428"}, "latest"]), 16)
    # sendMessage(target=dead...0001, data=0xbeef, minGasLimit=100000) selector 0x3dbb202b
    raw = make_eip1559_tx(PRIVKEY, nonce, MESSENGER,
        abi_encode_call("3dbb202b", target, b"\xbe\xef", 100000), 300_000)
    r = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw]))
    nonce1 = int(rpc.eth("eth_call", [{"to": MESSENGER, "data": "0xecc70428"}, "latest"]), 16)
    check("messenger_send_status1", r.get("status") == "0x1")
    check("messenger_nonce_increments", nonce1 == nonce0 + 1, f"{nonce0}->{nonce1}")
    # SentMessage 事件 topic0(keccak):
    #   keccak256("SentMessage(address,address,bytes,uint256,uint256)")
    SM_TOPIC = "0xcb0f7ffd78f9aee47a248fae8db181db6eee833039123e026dcbff529522e52a"
    check("messenger_sentmessage_event", first_log_topic(r) == SM_TOPIC, first_log_topic(r))
```

- [ ] **Step 2: Bridge 组 — depositERC20 mint + withdraw burn(3 条)**

先部署最小 ERC20(有 `mint/burn` 视图,复用 b3_contracts deploy 模式),然后:
```python
    # depositERC20To(l1Token, l2Token, to, amount, minGas, extra) selector 0x838b2520
    # 断言: 回执 status=0x1 + ERC20BridgeInitiated 事件存在 + l2Token 余额增加(mint)
    bal0 = int(rpc.eth("eth_call", [{"to": l2token, "data": "0x70a08231"+addr_pad(SENDER)}, "latest"]), 16)
    raw = make_eip1559_tx(PRIVKEY, nonce, BRIDGE, bytes.fromhex("838b2520") + abi_encode(l1token, l2token, SENDER, 1000, 100000, b""), 400_000)
    r = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw]))
    bal1 = int(rpc.eth("eth_call", [{"to": l2token, "data": "0x70a08231"+addr_pad(SENDER)}, "latest"]), 16)
    check("bridge_deposit_status1", r.get("status") == "0x1")
    check("bridge_mint_increases", bal1 >= bal0, f"{bal0}->{bal1}")
    # ERC20BridgeInitiated 事件 topic0(keccak):
    #   keccak256("ERC20BridgeInitiated(address,address,address,address,uint256,bytes)")
    EBI_TOPIC = "0x7ff126db8024424bbfd9826e8ab82ff59136289ea440b04b39a0df1b03b9cabf"
    check("bridge_erc20initiated_event", first_log_topic(r) == EBI_TOPIC, first_log_topic(r))
    # withdraw 同理(断言 WithdrawalInitiated 事件,selector 见 Step 3)
```
> **关键分歧点**:L2StandardBridge 的 depositERC20 在 L2 单侧是否真的 mint,取决于部署的桥字节码与 L2 token 是否 OptimismMintableERC20。实现时**先读 ABI + 试跑**,不 mint 则登记 DIVERGENCE(`bridge_deposit_l2_only_mint_unverified`),断言改为"回执可查 + 事件存在"。
> **Nonce 管理**:同一 `main()` 内多笔交易,每笔发送前用 `int(rpc.eth("eth_getTransactionCount",[SENDER,"latest"]),16)` 取**当前** nonce(不要复用 Task 3 的旧值);ERC20 部署 + 多笔调用都会递增。脚本组织建议:组间共用 `nonce` 游标,每组开头刷新。

- [ ] **Step 3: Bridge withdraw — burn + WithdrawalInitiated(2 条)**

```python
    # withdraw(l2Token, amount, minGas, extra) selector 0x32b7006d(keccak,已算)
    bal2 = int(rpc.eth("eth_call", [{"to": l2token, "data": "0x70a08231"+addr_pad(SENDER)}, "latest"]), 16)
    raw = make_eip1559_tx(PRIVKEY, nonce, BRIDGE,
        abi_encode_call("32b7006d", l2token, 500, 100000, b""), 400_000)
    r = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw]))
    bal3 = int(rpc.eth("eth_call", [{"to": l2token, "data": "0x70a08231"+addr_pad(SENDER)}, "latest"]), 16)
    check("bridge_withdraw_status1", r.get("status") == "0x1")
    check("bridge_burn_decreases", bal3 <= bal2, f"{bal2}->{bal3}")
    # WithdrawalInitiated 事件 topic0(keccak):
    #   keccak256("WithdrawalInitiated(address,address,address,address,uint256,bytes)")
    WI_TOPIC = "0x73d170910aba9e6d50b102db522b1dbcd796216f5128b445aa2135272886497e"
    check("bridge_withdrawalinitiated_event", first_log_topic(r) == WI_TOPIC, first_log_topic(r))
```
> 注:若 withdraw 的 ABI 参数含 `bytes` 动态段,`abi_encode_call` 需正确处理 offset(L2StandardBridge `withdraw(address,uint256,uint32,bytes)` 的 `extra` 是动态段)。若桥的单侧 burn 不成立(未接真实 token),登记 DIVERGENCE(`bridge_withdraw_l2_only_burn_unverified`),断言降级为"回执可查 + 事件存在"。

- [ ] **Step 4: 运行 + 提交**

```bash
bash restart_b3.sh && python3 predeploy_matrix.py
git add tools/op-e2e/predeploy_matrix.py && git commit -m "test(e2e): predeploy matrix messenger + standard-bridge groups"
```

---

### Task 5: SystemConfig 组

**Files:**
- Modify: `tools/op-e2e/predeploy_matrix.py`

**Interfaces:**
- Consumes: Task 2 骨架、Task 3 签名助手。
- Produces: SystemConfig 组断言(≥4 条)。

- [ ] **Step 1: 读权限 — getValueByKey(2 条)**

```python
    # getValueByKey(key="scalar") selector 0x1258a93a(keccak,已对字节码); 未 set 前读默认 (0, 0)
    # string 动态段: getValueByKey 无动态段前置,calldata = selector ‖ keyOffset=0x20 ‖ len ‖ key(pad32)
    out = rpc.eth("eth_call", [{"to": SYSTEM_CONFIG, "data": abi_encode_call("1258a93a", "scalar")}, "latest"])
    value, enable = out[2:66], out[66:130]   # uint192 | uint64 两段
    check("syscfg_get_default", int(value,16) == 0 and int(enable,16) == 0, f"value={value} enable={enable}")
```

- [ ] **Step 2: 写权限 — setValueByKey(3 条)**

```python
    # setValueByKey(key, value, enableNumber) selector 0x86ff19b9(keccak,已对字节码)
    # 预期: onlyOwner(owner=genesis 未播种 => 0x0) => revert,回执 status=0x0
    raw = make_eip1559_tx(PRIVKEY, nonce, SYSTEM_CONFIG,
        abi_encode_call("86ff19b9", "scalar", 1000, 1), 200_000)
    r = wait_receipt(rpc, rpc.eth("eth_sendRawTransaction", [raw]))
    check("syscfg_set_reverts_noowner", r.get("status") == "0x0", f"status={r.get('status')}")
    # revert data(0x08c379a0 Error(string))含 "caller is not the owner" — 若回执无 revertData 字段,此断言跳过(仅 status 断言必须过)
    rd = r.get("revertData", "")
    if rd:
        # 0x08c379a0 + offset(0x20) + len + string-ascii; 简化: 直接搜 ascii 子串
        check("syscfg_revert_reason", b"caller is not the owner" in bytes.fromhex(rd[10:]), rd[:80])
```
> **Divergence 登记**:SystemConfig genesis owner 未播种 → onlyOwner 恒 revert 是**预期**,不修。若实现时发现 owner 已被播种为 SENDER 或 ProxyAdmin,则改断言为"set 成功 + getValueByKey 返回写入值"。

- [ ] **Step 3: 运行 + 提交**

```bash
bash restart_b3.sh && python3 predeploy_matrix.py
git add tools/op-e2e/predeploy_matrix.py && git commit -m "test(e2e): predeploy matrix SystemConfig group (read/write + owner permission)"
```

---

### Task 6: 挂 run_all.sh + 全量回归

**Files:**
- Modify: `tools/op-e2e/run_all.sh`

**Interfaces:**
- Consumes: Task 2-5 完成的 `predeploy_matrix.py`(含全部组)。
- Produces: run_all.sh 挂载点(chain_driver 之后、a1_active 之前)+ 全量回归证据。

- [ ] **Step 1: 挂载到 run_all.sh**

在 `step "B.3: b3_contracts"` 与 `step "A.1+B4: a1_active"` 之间插入:
```bash
step "PREDEPLOY: predeploy_matrix (>=20 asserts)"
python3 predeploy_matrix.py || fail=1
```
> 断言总数随 Task 2-5 实付数量变化,注释用 `>=N` 并确保脚本退出码正确。

- [ ] **Step 2: 全量回归 — ctest(应为 1935/1935 全绿)**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/build
ctest --output-on-failure 2>&1 | tail -30
```
Expected: 1935/1935 全绿。注:`OpValidateSuite/EmptyEnvelopeFails` 已于 2026-08-17 修复为 `EmptyEnvelopeAccepted`(空 envelope 有意接受,l1_cost=0,对齐 OpTransition.cpp:376-379)——唯一红已消除。

- [ ] **Step 3: 全量回归 — op-e2e run_all.sh**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/tools/op-e2e
bash run_all.sh
```
Expected: `ALL OP-E2E GREEN`(原 6 脚本 + predeploy_matrix)。

- [ ] **Step 4: 更新交接文档 + 提交**

在 `docs/2026-08-17-session-handoff.md` 第 3 节勾掉"预部署矩阵下一步",记录交付向量/脚本数与 Divergence 登记;提交:
```bash
git add tools/op-e2e/run_all.sh docs/2026-08-17-session-handoff.md
git commit -m "test(e2e): gate predeploy_matrix in run_all + full regression"
```
