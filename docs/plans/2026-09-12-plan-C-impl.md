# Plan C 实施计划（C2 → C4 → C5 → C6 → C3）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把「缺语料/缺产物时静默 skip」的全部格子变成「真跑或明确失败」：M5 registry 扫描的 zip 路径可配置 + 空扫必失败（C2）、header RLP 字段集逐档基线（C4）、genesis 层 pre-Karst 口径守卫（C5）、30-target 计数表 + PR gate 预算实测（C6）、nightly/weekly workflow（C3）。C1（getpayload 接线）**已完成**：语料 `a908a6d0`（双远端已推）+ FISCO `fda7d2b48`（bump + F-CI-2），`ci_pins` 门在 run 34737509739 上 success。

**Architecture:** 本计划是 `karst-on-5550/docs/plans/2026-09-12-plan-C-corpus-ci.md` 的执行计划（冲突时以它为准，以本文件的实测修正为准——本文件含 7 处已批准修正）。编排 A：零授权项（C2/C4/C5）先行、观测项（C6）次之、推送依赖项（C3）最后。TDD + 每条新守卫必带变异反例；构建串行；`docs/**`/`.agents/**` 永不入库；禁止目录级 `git add <dir>/`。

**Tech Stack:** C++20 / Boost.Test（`--run_test` 一 suite 一调用、计数只认 `Running N test cases`、不得用 `errors detected` 子串判红）；Python 3.11 + pytest 7.4.3（本机仅 `python3.11` 有 pytest）；GitHub Actions（复用既有 vcpkg bootstrap + `actions/cache@v4` 段）。

**工作区：** FISCO 代码只在 `/Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal`（分支 `feat/karst-on-318-merged`）。语料仓 `~/.cache/fisco-t8n-corpus`（分支 `feat/test-matrix-oracles`）**本计划零改动**（C1 已完成；C4 走「补齐」不新增语料工件；C5 选项 a 不动 zip）。只读：主仓脏工作区、`karst-on-5550`。参考 pin：op-geth `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`、语料 ref `a908a6d08af64fa6d3ef1744c3da079b92006bce`。

**推送：** C2/C4/C5/C6 的提交按既有授权推 fork `ywy2090/FISCO-BCOS` 的 `feat/karst-on-318-merged`（不碰 `fisco/release-3.18.0`）；C3 的 dispatch 验收**待合入**（GitHub 要求 workflow 存在于默认分支，fork 默认分支是 `master`）——本地只做空跑反例 + YAML 校验。

---

## 实测基线（写测试前必读的真值表，全 112 个顶层 golden + 6 个 chained golden 实测）

1. **golden 顶层键没有 `_info`**：只有 `blockHash/encodedHeaderHex/excessBlobGas/extraData/rawTransactions/transactionsRoot`。**档位只能从文件名前缀推**（`regolith_`/`canyon_`/…），且 3 个边界 golden 的前缀会说谎（见 3）。
2. **header RLP 列表长度 = 最后一个非 nil 尾槽的下标**（级联 0x80 占位：尾部 nil 字段整体省略；**中段零值字段是 0x80 槽位=在场但为零**，如 `blobGasUsed=0`）。因此判「字段集」的口径是**列表长度 + 首槽/尾槽非空不变式**，不是"逐槽非空"。
3. **逐档期望长度（实测，非文档转抄）**：

| fork（档） | 列表项数 | 非空不变式 | 实测文件数 |
|---|---|---|---|
| regolith | 16 | slot15 `baseFeePerGas` 非空 | 5 |
| canyon | 17 | + slot16 `withdrawalsRoot` 非空 | 5 |
| canyon 边界×2 | **20** | 同 ecotone | 2 |
| ecotone | 20 | slot19 `parentBeaconBlockRoot` 非空（slot17/18 blob 对可为 0x80 零值） | 8 |
| fjord | 20（边界×1 为 21） | 同上 | 10+1 |
| granite | 20 | 同上 | 3 |
| holocene | 20 | 同上 | 4 |
| isthmus | 21 | slot20 `requestsHash` 非空 | 46 |
| jovian | 21 | 同上（`jovian_da_mix` 的 slot17 `blobGasUsed` 非空=DA 用例的值差异，形状不变） | 28 |
| chained 全部 6 个 `*.golden.json` | 21 | 同 isthmus/jovian | 6 |
| karst | —（**无 golden**） | 由 `rebuildOpEthHeader` 门控与 op-geth 映射表**推出**：21 且无 `slotNumber`，只许 `BOOST_TEST_MESSAGE` 标注 | 0 |

4. **文件名前缀覆盖表**（边界 golden 的档位修正）：`canyon_boundary_ecotone_synth` → ecotone、`canyon_boundary_ecotone_activation` → ecotone、`fjord_upgrade_isthmus_activation` → isthmus（`isthmus_upgrade_jovian_activation` 无需修正：jovian==isthmus 形状）。
5. **任何档最大 21 项**（`slotNumber` 属 EIP-7843/Amsterdam，op-geth pin 无 OP fork 映射）——这是"恒缺"断言。**本仓 `EthBlockHeaderData` 只有 6 个 optional 尾字段、没有 slotNumber**（`bcos-rlp-protocol/.../EthBlockHeader.h:63-68`），所以"出现第 22 槽"会被 Task 2 的 byte-equivalence 直接抓住（21 字段模型无法逐字节复现 22 项输入）。
6. 计划原文的"Ecotone 19"是**错的**（实测 20）；"110/116 个 golden"已过时（现 112 顶层 + 6 chained golden）。
7. **Task 2 用的 API（已核实存在且可在 engine 测试里用）**：`bcos::protocol::EthBlockHeader`（`void rlpEncode(bcos::bytes&) const`、`bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef)`、`const EthBlockHeaderData& data()`），头文件 `#include <bcos-rlp-protocol/EthBlockHeader.h>`（同目录 `EngineServiceTest.cpp:39` 已在用）；`std::optional` 六件套即尾部字段的存在性来源。**不要**走 `toTarsHeader` → `BlockHeaderImpl`：它把可选字段投影掉，absence 会丢失（这也是 F-B2-1 那条哨兵语义的同源问题）。
8. 审查后**删除了初版的手写 RLP 遍历器**（其 `prefix < 0x80` 单字节分支下溢、首跑必错），Task 2 改为「重编码逐字节等价 + 解码后可选集不变性」。

---

### Task 0: 基线测量（不改任何文件）

**Files:** 无（只读 + /tmp）

- [ ] **Step 0.1: 三个 binary 的用例数基线（改动前后各报一次的"前"）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
for b in build/engine/test/test-bcos-engine \
         build/opstack-executor/tests/opstack-executor-block-tests \
         build/bcos-evm/test/bcos-evm-opstack-tests; do
  printf '%s -> ' "$b"
  "$b" 2>&1 | grep -m1 -oE 'Running [0-9]+ test cases'
done
```

Expected（2026-09-13 实测基线）：engine `Running 350 test cases`；opstack `Running 146 test cases`；bcos-evm-opstack `Running 183 test cases`。记录到汇报。（**勘误**：本计划初稿写的 engine 336 是从别处台账误抄的 rpc 二进制数字，实测 350。）

- [ ] **Step 0.2: pytest 基线**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
python3.11 -m pytest tools/opstack-genesis/test_gen_official_genesis.py -q 2>&1 | tail -3
```

Expected: `29 passed`（pytest 用例数，实测）。**注意别把 60/59/1 当成 pytest 数字**——那是 registry sweep 内部的**链**计数（60 条链 / 59 通过 / 1 排除），由 sweep 测试自己的 `set(passed) == set(chains) - exclusions` 断言，不在 pytest 汇总里出现。

---

### Task 1 (C2/WI-11): zip 路径 env 化 + 缺档必失败

**Files:**
- Modify: `tools/opstack-genesis/test_gen_official_genesis.py`（`:4-8` imports、`:343` 常量、`:375-377` 与 `:399-401` 两处 skip 门）

- [ ] **Step 1.1: 写会红的守卫（先加 import 与常量改造）**

`:4-8` 的 import 区（现无 `os`）改为：

```python
import importlib.util
import json
import os
import re
import zipfile
from pathlib import Path
```

`:343` 常量改为：

```python
# Registry zip: a machine default kept for the local ritual, overridable for CI
# (the nightly points OP_GETH_ZIP at the op-geth pin tree). When
# OP_REQUIRE_REGISTRY_ZIP=1 a missing zip is a FAILURE, not a skip — a silent
# skip here is how M5 degraded to "green but vacuous" (WI-11).
_DEFAULT_OP_GETH_ZIP = Path("/Users/octopus/octo/code/op-geth/superchain/superchain-configs.zip")
_OP_GETH_ZIP = Path(os.environ.get("OP_GETH_ZIP", _DEFAULT_OP_GETH_ZIP))
```

（注意：**不采用**计划原文的 `<repo>/op-geth` 默认值——该路径不存在，会打破本地 60/59/1 基线。）

- [ ] **Step 1.2: 两处 skip 门改为「REQUIRE 则 fail」**

两处（`:375-377` 的 `test_real_registry_full_sweep_matches_documented_exclusions` 与 `:399-401` 的 `test_real_registry_alt_da_chains_emit_alt_da`）的同一模式：

```python
    import shutil
    if not _OP_GETH_ZIP.exists() or shutil.which("zstd") is None:
        if os.environ.get("OP_REQUIRE_REGISTRY_ZIP") == "1":
            pytest.fail(f"OP_REQUIRE_REGISTRY_ZIP=1 but registry zip/zstd unavailable "
                        f"(zip={_OP_GETH_ZIP})")
        pytest.skip("op-geth superchain zip / zstd CLI not available")
```

**另两处 skip 必须显式处置（审查 F3：实测共 4 处，计划初版只覆盖了 2 处）**：`:440-441`（`test_undecodable_registry_frame_is_a_registry_error`）与 `:448-449`（`test_cli_reports_undecodable_frame_without_traceback`）只依赖 `zstd`、用**合成 zip**，不读 registry —— 保持 skip，但在各自 `pytest.skip` 上方加一行注释说明豁免理由：

```python
    # Exempt from OP_REQUIRE_REGISTRY_ZIP: this case builds its own synthetic zip
    # and only needs the zstd CLI (the registry zip is never read here).
```

全链扫描的空扫守卫已有（`assert chains`，`:379`）——保留并加注释 `# empty sweep is a failure, not a pass`。

- [ ] **Step 1.3: 红证（REQUIRE + 缺 zip → FAIL）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
OP_GETH_ZIP=/nonexistent OP_REQUIRE_REGISTRY_ZIP=1 python3.11 -m pytest \
  tools/opstack-genesis/test_gen_official_genesis.py -k "full_sweep or alt_da" -q 2>&1 | tail -4
```

Expected: `failed`（`OP_REQUIRE_REGISTRY_ZIP=1 but registry zip/zstd unavailable`），**不是** `skipped`。

- [ ] **Step 1.4: 绿证（默认路径基线不变）**

```bash
python3.11 -m pytest tools/opstack-genesis/test_gen_official_genesis.py -q 2>&1 | tail -2
```

Expected: `29 passed`（与 Task 0.2 相同——改路径来源不改行为；Task 1 不新增用例）。

- [ ] **Step 1.5: 提交**

```bash
git add tools/opstack-genesis/test_gen_official_genesis.py
git commit -m "test(genesis): make the registry zip path configurable and fail on a missing zip (WI-11)"
```

---

### Task 2 (C4/WI-13): header RLP 字段集逐档基线（重编码等价 + 可选集不变式）

**Files:**
- Modify: `tools/opstack-genesis/gen_official_genesis.py`（`header_field_set`，`:107-121`）
- Modify: `tools/opstack-genesis/test_gen_official_genesis.py`（`test_header_field_set_by_genesis_time`，`:113-119`）
- Create: `engine/test/unittests/engine/OpHeaderFieldSetBaselineTest.cpp`
- Test: engine target（GLOB → 改完必须 `cmake -B build -S .` 重配）

- [ ] **Step 2.1: Python 侧扩 jovian/karst 档（不改 isthmus 及之前的行）**

`header_field_set` 的 `isthmus` 分支之后追加：

```python
    if ts0 >= fork_times.get("jovian", inf):
        pass  # Jovian does not change the header field set (DA footprint reuses blob_gas_used)
    if ts0 >= fork_times.get("karst", inf):
        pass  # Karst does not change the header field set (Osaka EL ruleset; no new header field)
```

（`slot_number` 属 EIP-7843/Amsterdam，op-geth 的 OP 映射表无此档——任何分支都不得加入。）

- [ ] **Step 2.2: Python 用例补断言**

`test_header_field_set_by_genesis_time` 末尾追加：

```python
    forks7 = {"canyon": 100, "ecotone": 200, "isthmus": 400, "jovian": 500, "karst": 600}
    assert gen.header_field_set(500, forks7) == gen.header_field_set(400, forks7)
    assert gen.header_field_set(600, forks7) == gen.header_field_set(400, forks7)
    for ts in (0, 100, 200, 400, 500, 600):
        assert "slot_number" not in gen.header_field_set(ts, forks7)
```

- [ ] **Step 2.3: 跑 Python 侧**

```bash
python3.11 -m pytest tools/opstack-genesis/test_gen_official_genesis.py -k header_field_set -q 2>&1 | tail -2
```

Expected: `1 passed`。

- [ ] **Step 2.4: 新建 C++ 基线测试（完整文件）**

Create `engine/test/unittests/engine/OpHeaderFieldSetBaselineTest.cpp`：

```cpp
// OpHeaderFieldSetBaselineTest.cpp — per-fork header RLP field-set baseline (Plan C / WI-13).
//
// Two independent halves over the whole golden corpus:
//
//   (a) BYTE-EQUIVALENCE (decisive): decode each golden's encodedHeaderHex into the
//       RLP-domain EthBlockHeader and re-encode it — the bytes must come back identical.
//       This fails on ANY divergence in optional-field presence, order or value, which
//       is exactly what "the field set changed at fork X" looks like from outside. It
//       also subsumes the slotNumber requirement: a golden carrying a 22nd tail slot
//       cannot round-trip through this repo's 21-field model (EthBlockHeaderData has
//       exactly six std::optional tails).
//
//   (b) PRESENCE INVARIANTS (readable): read those six optionals off the decoded header
//       and assert (i) uniformity within a fork, (ii) monotonicity along the fork order,
//       (iii) the documented boundary facts. The observed table is printed for the record.
//
// Staying in the RLP domain is deliberate: BlockHeaderImpl (what toTarsHeader produces)
// PROJECTS the optionals — FISCO's op header carries present-zero sentinels — so going
// through it would hide absence and make this test vacuous.
//
// FISCO codec semantics (verified, bcos-codec/rlp/RLPDecode.h:307-320): optional presence
// is decided by "the input view is exhausted", so geth's middle 0x80 slot decodes to a
// PRESENT zero (u256 0) — presence here matches geth's slot shape exactly. The encoder
// (RLPEncode.h:131-137) OMITS a nullopt entirely (no 0x80 placeholder): re-encoding the
// goldens cannot diverge because geth bytes always decode to a contiguous present prefix,
// but a genuine middle-nullopt would round-trip differently — the invariant checks below
// pin that prefix shape per fork.
//
// Oracle: op-geth e8800cffe core/types/block.go (Header = 15 required + 7 optional tail
// slots, order baseFeePerGas, withdrawalsRoot, blobGasUsed, excessBlobGas,
// parentBeaconBlockRoot, requestsHash, slotNumber) and core/types/gen_header_rlp.go
// (a tail slot is emitted iff itself or any LATER slot is non-nil; a nil middle slot is
// written as 0x80), plus this repo's rebuildOpEthHeader gating
// (engine/bcos-engine/OpEngineService.cpp:406-470: >= Canyon withdrawalsRoot, >= Ecotone
// blob pair + beacon root, >= Isthmus requestsHash, Jovian/Karst add nothing).
//
// Karst has NO golden in the corpus (regolith..jovian only): its row is asserted by
// construction and printed as such — never presented as measured.

#include <bcos-rlp-protocol/EthBlockHeader.h>   // EthBlockHeader::rlpDecode/rlpEncode, data()
#include <bcos-utilities/DataConvertUtility.h>  // bcos::fromHex, bcos::ref
#include <json/json.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace
{

// Canonical fork order of the corpus (regolith..jovian; karst has no golden).
constexpr char const* c_forkOrder[8] = {"regolith", "canyon", "ecotone", "fjord",
                                        "granite", "holocene", "isthmus", "jovian"};

int forkRank(std::string const& fork)
{
    for (int i = 0; i < 8; ++i)
    {
        if (fork == c_forkOrder[i])
        {
            return i;
        }
    }
    return -1;
}

// Boundary goldens carry the NEW fork's header although the case name keeps the old
// prefix (activation / synthesis blocks). Probed: these three and only these.
std::string forkOfGolden(std::string const& filename)
{
    static std::map<std::string, std::string> const c_overrides = {
        {"canyon_boundary_ecotone_synth.golden.json", "ecotone"},
        {"canyon_boundary_ecotone_activation.golden.json", "ecotone"},
        {"fjord_upgrade_isthmus_activation.golden.json", "isthmus"},
    };
    if (auto it = c_overrides.find(filename); it != c_overrides.end())
    {
        return it->second;
    }
    return filename.substr(0, filename.find('_'));
}

// chained/ names carry no fork prefix: the chain cases are isthmus-era, the jovian*
// ones jovian-era (both tiers share the same tail set, so this only affects reporting).
std::string chainedForkOf(std::string const& name)
{
    return name.rfind("jovian", 0) == 0 ? std::string{"jovian"} : std::string{"isthmus"};
}

// The six optional tail slots, in RLP order, rendered as a printable presence tuple.
// Read off the DECODED header, so presence is the decoder's positional model (a later
// slot present implies the earlier ones were present) — no hand-written RLP walk.
std::string tailPresence(bcos::protocol::EthBlockHeaderData const& d)
{
    std::string out;
    auto add = [&out](bool present, char const* name) {
        if (present)
        {
            if (!out.empty())
            {
                out += "+";
            }
            out += name;
        }
    };
    add(d.baseFee.has_value(), "baseFeePerGas");
    add(d.withdrawalsHash.has_value(), "withdrawalsRoot");
    add(d.blobGasUsed.has_value(), "blobGasUsed");
    add(d.excessBlobGas.has_value(), "excessBlobGas");
    add(d.parentBeaconRoot.has_value(), "parentBeaconBlockRoot");
    add(d.requestsHash.has_value(), "requestsHash");
    return out;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpHeaderFieldSetBaselineSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(GoldenHeaderFieldSetAndReencodeMatchForkBaseline, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto const dir = fs::path(OP_T8N_GOLDEN_ENGINE_DIR);
    if (!fs::exists(dir))
    {
        // House pattern (see OpEnginePayloadShapeBaselineTest.cpp:44-52): absent locally
        // is a skip with a pointer to the ritual; absent in CI is a FAILURE, because
        // GITHUB_ACTIONS defines FISCO_REQUIRE_T8N_CORPUS and a silently empty sweep is
        // exactly the degradation this test exists to prevent.
#ifdef FISCO_REQUIRE_T8N_CORPUS
        BOOST_FAIL("CI requires the golden corpus at " << dir.string()
                                                      << " -- run the t8n regen ritual");
#else
        BOOST_TEST_MESSAGE("golden corpus absent; run the corpus regen ritual -- skipping");
        return;
#endif
    }

    std::map<std::string, std::string> presenceByFork;      // fork -> tail presence tuple
    std::map<std::string, std::uint32_t> filesByFork;       // fork -> golden count
    std::uint32_t filesChecked = 0;

    auto checkOne = [&](std::string const& name, std::string const& hexWithPrefix,
                        std::string const& fork) {
        BOOST_TEST_INFO_SCOPE(name);
        BOOST_REQUIRE_GT(hexWithPrefix.size(), 2U);
        auto const rank = forkRank(fork);
        BOOST_REQUIRE_MESSAGE(rank >= 0, name << ": unknown fork tier " << fork);

        auto bytes = bcos::fromHex(hexWithPrefix);  // tolerates the 0x prefix
        BOOST_REQUIRE(!bytes.empty());
        bcos::protocol::EthBlockHeader eth;
        auto err = eth.rlpDecode(bcos::ref(bytes));
        BOOST_REQUIRE_MESSAGE(err == nullptr, name << ": rlpDecode of encodedHeaderHex failed: "
                                                  << (err ? err->errorMessage() : ""));
        bcos::bytes reencoded;
        eth.rlpEncode(reencoded);

        // (a) decisive: our encoder must reproduce op-geth's bytes exactly. On mismatch
        // localize with the same data the decoder already gave us (no RLP walk needed).
        if (reencoded != bytes)
        {
            std::size_t i = 0;
            while (i < std::min(reencoded.size(), bytes.size()) && reencoded[i] == bytes[i])
            {
                ++i;
            }
            BOOST_CHECK_MESSAGE(false,
                name << ": re-encode differs from the golden header -- first difference at byte " << i
                     << " (golden " << bytes.size() << "B, re-encoded " << reencoded.size()
                     << "B); decoded tail presence [" << tailPresence(eth.data())
                     << "]. A field-set/order change at this fork, or a divergence in the "
                        "optional-tail semantics, must be registered in DIVERGENCES.md.");
        }

        // (b) presence invariants: uniform within a tier.
        auto const tuple = tailPresence(eth.data());
        if (auto it = presenceByFork.find(fork); it == presenceByFork.end())
        {
            presenceByFork[fork] = tuple;
        }
        else
        {
            BOOST_CHECK_MESSAGE(it->second == tuple,
                name << ": fork " << fork << " tail presence [" << tuple
                     << "] differs from this tier's other goldens [" << it->second << "]");
        }
        ++filesByFork[fork];
        ++filesChecked;
    };

    for (auto const& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
            entry.path().stem().extension() != ".golden")
        {
            continue;
        }
        std::ifstream in(entry.path());
        Json::Value doc;
        BOOST_REQUIRE_MESSAGE(Json::Reader{}.parse(in, doc, false),
            "malformed golden json: " << entry.path().string());
        checkOne(entry.path().filename().string(), doc["encodedHeaderHex"].asString(),
            forkOfGolden(entry.path().filename().string()));
    }
    auto const chained = dir / "chained";
    if (fs::exists(chained))
    {
        for (auto const& entry : fs::directory_iterator(chained))
        {
            auto name = entry.path().filename().string();
            if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
                name.find(".golden.json") == std::string::npos)
            {
                continue;
            }
            std::ifstream in(entry.path());
            Json::Value doc;
            BOOST_REQUIRE_MESSAGE(Json::Reader{}.parse(in, doc, false),
                "malformed chained golden: " << entry.path().string());
            checkOne(name, doc["encodedHeaderHex"].asString(), chainedForkOf(name));
        }
    }

    // ---- boundary facts + monotonicity (the readable half of the baseline) ----
    for (auto const& [fork, tuple] : presenceByFork)
    {
        auto const rank = forkRank(fork);
        BOOST_CHECK_MESSAGE(tuple.find("baseFeePerGas") != std::string::npos,
            fork << ": baseFeePerGas must be present on every Eth header tier");
        if (rank >= 1)
            BOOST_CHECK_MESSAGE(tuple.find("withdrawalsRoot") != std::string::npos,
                fork << ": Canyon+ must carry withdrawalsRoot");
        else
            BOOST_CHECK_MESSAGE(tuple.find("withdrawalsRoot") == std::string::npos,
                fork << ": pre-Canyon must NOT carry withdrawalsRoot");
        if (rank >= 2)
        {
            BOOST_CHECK_MESSAGE(tuple.find("parentBeaconBlockRoot") != std::string::npos,
                fork << ": Ecotone+ must carry parentBeaconBlockRoot");
            BOOST_CHECK_MESSAGE(tuple.find("blobGasUsed") != std::string::npos,
                fork << ": Ecotone+ must carry the blob pair");
            BOOST_CHECK_MESSAGE((tuple.find("blobGasUsed") != std::string::npos) ==
                                    (tuple.find("excessBlobGas") != std::string::npos),
                fork << ": blobGasUsed/excessBlobGas must appear together");
        }
        else
            BOOST_CHECK_MESSAGE(tuple.find("parentBeaconBlockRoot") == std::string::npos,
                fork << ": pre-Ecotone must NOT carry parentBeaconBlockRoot");
        if (rank >= 6)
            BOOST_CHECK_MESSAGE(tuple.find("requestsHash") != std::string::npos,
                fork << ": Isthmus+ must carry requestsHash");
        else
            BOOST_CHECK_MESSAGE(tuple.find("requestsHash") == std::string::npos,
                fork << ": pre-Isthmus must NOT carry requestsHash");
    }
    for (int i = 1; i < 8; ++i)
    {
        auto const lo = presenceByFork.find(c_forkOrder[i - 1]);
        auto const hi = presenceByFork.find(c_forkOrder[i]);
        if (lo == presenceByFork.end() || hi == presenceByFork.end())
        {
            continue;
        }
        for (auto const* field : {"baseFeePerGas", "withdrawalsRoot", "blobGasUsed",
                 "excessBlobGas", "parentBeaconBlockRoot", "requestsHash"})
        {
            if (lo->second.find(field) != std::string::npos)
            {
                BOOST_CHECK_MESSAGE(hi->second.find(field) != std::string::npos,
                    c_forkOrder[i - 1] << " carries " << field << " but " << c_forkOrder[i]
                                       << " does not -- per-fork field sets must only grow");
            }
        }
    }

    // The sweep must actually compare the corpus, and at least eight distinct forks
    // (regolith..jovian) must be present — otherwise this degraded to an empty pass.
    BOOST_CHECK_GE(filesChecked, 100U);
    BOOST_CHECK_MESSAGE(presenceByFork.size() >= 8,
        "compared " << presenceByFork.size() << " fork tiers, need >= 8");
    for (auto const* fork : c_forkOrder)
    {
        if (auto it = presenceByFork.find(fork); it != presenceByFork.end())
        {
            BOOST_TEST_MESSAGE("  " << fork << ": " << filesByFork[fork] << " golden(s), tail presence ["
                                    << it->second << "]");
        }
    }
    BOOST_TEST_MESSAGE("karst: NO golden in the corpus -- asserted by construction, not measured: "
                       "same tail set as jovian (no new header field on the Osaka EL ruleset) and "
                       "no slotNumber; the byte-equivalence half above is what would catch its "
                       "appearance in a future corpus.");
}
BOOST_AUTO_TEST_SUITE_END()
```

（若 unity 编译报宏/符号冲突，给该文件补 `set_source_files_properties(... SKIP_UNITY_BUILD_INCLUSION ON)`——与同目录 parity 测试同款；先不加，遇到再加。）

**为什么没有手写 RLP 尾槽遍历（审查 F1/F2）**：初版计划写了一个 RLP 列表遍历器，其中 `prefix <= 0xb7` 分支在 `prefix < 0x80`（单字节项，如小值 `number`）时下溢，首跑必错；而且那个遍历器本身不必要——`EthBlockHeader::rlpDecode` 已经把尾部可选字段解成 `std::optional`，presence 直接可读（spec 的 C4 补齐原意正是"不必手写遍历"）。改为「重编码逐字节 + 解码后的可选集不变式」后，既消掉了那类解析缺陷，又把断言强度从"长度对不对"提升到"我方编码器必须复现 op-geth 的字节"。

- [ ] **Step 2.5: 重配 + 编译 + 绿证**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
cmake -B build -S . && ninja -C build test-bcos-engine
./build/engine/test/test-bcos-engine --run_test=OpHeaderFieldSetBaselineSuite --log_level=test_suite 2>&1 \
  | grep -E 'Running|golden\(s\)|karst|No errors'
```

Expected: `Running 1 test cases`；八行 `<fork>: <n> golden(s), tail presence [...]`，计数含边界覆盖与 chained 归并后的精确值：`regolith: 5`（[baseFeePerGas]）、`canyon: 5`（[baseFeePerGas+withdrawalsRoot]，2 个 ecotone 边界已移出）、`ecotone: 10`（+2 边界）、`fjord: 10`（1 个 isthmus 边界已移出）、`granite: 3`、`holocene: 4`、`isthmus: 51`（46+1 边界+chained 4）、`jovian: 30`（28+chained 2），合计 118；一行 `karst: NO golden in the corpus -- asserted by construction ...`；`*** No errors detected`。若 byte-equivalence 在任何档失败 → **停**：那是实质发现，按 spec「差异即 finding」登记 `DIVERGENCES.md`（`^###` 小节）后再决定，不得改期望表。

- [ ] **Step 2.6: 变异反证（证明 byte-equivalence 那半有牙）**

在 `eth.rlpEncode(reencoded);` 之后临时插入一行 `reencoded[3] ^= 0x01;` → 重编译重跑：

```bash
ninja -C build test-bcos-engine && ./build/engine/test/test-bcos-engine \
  --run_test=OpHeaderFieldSetBaselineSuite 2>&1 \
  | grep -cE 're-encode differs from the golden header'
```

Expected: ≥100（每个 golden 各一条，含首个差异字节定位与 decoded tail presence）。**删除该行 → 重编译 → 复绿**。

- [ ] **Step 2.7: 全 binary 复验 + 提交**

```bash
./build/engine/test/test-bcos-engine 2>&1 | grep -m1 -oE 'Running [0-9]+ test cases'   # 预期 351（实测基线 350 + 1）
git add engine/test/unittests/engine/OpHeaderFieldSetBaselineTest.cpp \
        tools/opstack-genesis/gen_official_genesis.py tools/opstack-genesis/test_gen_official_genesis.py
git commit -m "test(engine): pin the per-fork header RLP field-set baseline over the golden corpus (WI-13)"
```

（engine 是 file(GLOB_RECURSE)：新文件靠 Step 2.5 的重配进入构建；**不需要**改 CMakeLists——计划原文的 `git add engine/test/CMakeLists.txt` 作废。）

---

### Task 3 (C5/WI-14): genesis 层 pre-Karst 口径守卫（选项 a）

**Files:**
- Modify: `tools/opstack-genesis/test_gen_official_genesis.py`（在 `test_check_registry_rollup_still_rejects_canonical_regression` 之后追加两个用例）

已裁决口径（用户批准）：**保持 pre-Karst zip + 既有 `--extra-fork karst` overlay**；Lagoon 项**删除**（zip 实测 `lagoon_time` 0 命中、生成器无该键；只在本 Task docstring 与台账记理由，karst-on-5550 只读不改）。

- [ ] **Step 3.1: 追加守卫 + 自带的合成反例（永远在 suite 里证明守卫会红）**

```python
def _assert_registry_zip_is_pre_karst(zip_path):
    """M5's registry basis is the pre-Karst pin (zip COMMIT 9cf0456a).

    If this guard goes red the corpus has moved past Karst: the M5 basis, the
    minimal read set and the documented 60/59/1 split must be re-decided in the
    SAME commit that swaps the zip — never silently.
    """
    scanned = 0
    with zipfile.ZipFile(zip_path) as zf:
        for name in zf.namelist():
            if name.startswith("configs/") and name.endswith(".toml"):
                assert b"karst_time" not in zf.read(name), (
                    f"{name} carries karst_time — the registry is post-Karst; "
                    "re-baseline M5 (basis + minimal read set) in the same commit")
                scanned += 1
    assert scanned > 0, "scanned no configs/*.toml — the guard is green but empty"


def test_registry_zip_is_pre_karst_basis():
    _assert_registry_zip_is_pre_karst(_OP_GETH_ZIP)


def test_pre_karst_guard_goes_red_on_a_synthetic_karst_entry(tmp_path):
    # Permanent mutation evidence: the guard above must be able to fail.
    zip_path = tmp_path / "post-karst.zip"
    with zipfile.ZipFile(zip_path, "w") as zf:
        zf.writestr("configs/mainnet/base.toml", "karst_time = 1\n")
    with pytest.raises(AssertionError, match="re-baseline M5"):
        _assert_registry_zip_is_pre_karst(zip_path)
```

- [ ] **Step 3.2: 跑 + 红绿两证**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
python3.11 -m pytest tools/opstack-genesis/test_gen_official_genesis.py -k pre_karst -q 2>&1 | tail -2
```

Expected: `2 passed`（真 zip 绿 + 合成 zip 证明能红）。

- [ ] **Step 3.3: 提交**

```bash
git add tools/opstack-genesis/test_gen_official_genesis.py
git commit -m "test(genesis): pin the pre-Karst registry basis with a guard that can fail (WI-14)"
```

---

### Task 4 (C6/WI-21): target 计数表（实测 40 行）+ PR gate 预算实测

**Files:**
- Create（不入库）: `docs/plans/2026-09-12-target-counts.md`

- [ ] **Step 4.1: 采全量 target→count（修正计划原文漏括号的 find，并排除脚本）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
find build -type f -perm +111 \( -name 'test-*' -o -name '*-tests' \) \
     ! -name '*.sh' ! -name '*.py' ! -name '*.cmake' | sort -u | while read -r t; do
  n=$("$t" 2>&1 | grep -m1 -oE 'Running [0-9]+ test cases' | grep -oE '[0-9]+')
  printf '%s\t%s\n' "$t" "${n:-SKIP/ERR}"
done | tee /tmp/target-counts.txt
```

Expected: **40 行**（审查 F5 实测：`build/` 下可执行测试二进制 40 个，其中 `opstack-executor-*-tests` 有 5 个——block/detail/receipt/scheduler/tests；设计文档 §6.9 写的"30 行"已过时，验收按实测 40 行改写，并在汇报/台账记明该口径变更）。`ctest -N` 是 2561 个**用例**、不是 target 口径，不要用它。
**两个已知预存失败先认出来，不算本轮回归**：`test-bcos-executor` 的 `testSM3AndKeccak256`（两棵树都红）、bcos-boostssl 的 `WsToolsTest`（Boost 1.88 行为，F-TEST-1 已立案）。其余非零 rc：先与基线树对比再定性。

- [ ] **Step 4.2: 从现成 CI run 读 step 计时（不加 instrumentation 再推一次）**

```bash
gh api repos/ywy2090/FISCO-BCOS/actions/runs/34737509739/jobs \
  --jq '.jobs[] | select(.name | test("Build \\(ubuntu")) | .steps[] | select(.conclusion != null) |
        "\(.name)\t\(.conclusion)\t\(.started_at)"'
gh run view 34737509739 --repo ywy2090/FISCO-BCOS --json jobs \
  --jq '.jobs[] | "\(.name)\t\(.started_at)\t\(.completed_at)"'
```

（fork-matrix 相关步 = Regenerate opstack t8n vectors / Test / Fork-label coverage guard / mutation 门。若 5 分钟超限的判据被触发，才动 `workflow.yml` 的分档并单独提交；否则 **Task 4 不产生 git 提交**——计数表是过程文档。）

- [ ] **Step 4.3: 落盘计数表**

把 `/tmp/target-counts.txt` + 步骤计时结论 + 两个预存失败的定性写进 `docs/plans/2026-09-12-target-counts.md`（untracked，永不入库）。

---

### Task 5 (C3/WI-12): nightly / weekly workflow（dispatch 验收待合入）

**Files:**
- Create: `.github/workflows/opstack-fork-nightly.yml`
- Create: `.github/workflows/opstack-fork-weekly.yml`

对计划原文的修正（已批准）：①suite↔binary 实测映射（`OpGoldenCorpusProvenance*` → `opstack-executor-block-tests`；`OpPrecompilesSuite`/`OpOsakaSemanticsSuite` → `bcos-evm-opstack-tests`；只有两个 shape/sweep suite 在 engine）——原文把 5 个 suite 全挂 engine，首跑必红；②语料段复用 composite regen action（同 `workflow.yml` 的 `@a908a6d0…` ref）而非只 provision——否则 golden 是 gitignored 的，`OpGoldenCorpusProvenance` 在 nightly 必红；③M5 的 zip 从 action 缓存的 op-geth pin 树取（`${{ runner.temp }}/op-geth`，zip 在 pin 树内被跟踪，实测）；④`python3.11` 由 `actions/setup-python@v5` 显式提供（runner 自带 3.12 无本仓依赖假设）。

- [ ] **Step 5.1: 写 nightly（完整文件）**

Create `.github/workflows/opstack-fork-nightly.yml`：

```yaml
name: opstack-fork-nightly
on:
  schedule:
    - cron: "0 3 * * *"   # 03:00 UTC daily
  workflow_dispatch:
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
jobs:
  fork-matrix:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v3
        with:
          clean: false
      - name: Initialize vcpkg submodule
        run: git submodule update --init --recursive -- vcpkg
      - name: Compute vcpkg revision
        id: vcpkg-revision
        shell: bash
        run: echo "revision=$(git -C vcpkg rev-parse HEAD)" >> "$GITHUB_OUTPUT"
      - uses: lukka/get-cmake@latest
        with:
          cmakeVersion: 3.28.3
          ninjaVersion: 1.12.1
      - name: Restore vcpkg cache
        uses: actions/cache@v4
        with:
          path: ${{ github.workspace }}/build/vcpkg_installed
          key: vcpkg-${{ hashFiles('vcpkg.json', 'vcpkg-configuration.json', 'ports/**') }}-${{ steps.vcpkg-revision.outputs.revision }}-${{ runner.os }}-${{ runner.arch }}-cache-key-v4
      - name: Regenerate opstack t8n vectors (corpus + goldens + getpayload envelopes)
        uses: FISCO-BCOS/op-stack-e2e-tests/.github/actions/opstack-t8n-regen@a908a6d08af64fa6d3ef1744c3da079b92006bce
      - name: Configure + build the test binaries (suites + every mutation-harness target)
        run: |
          mkdir -p build && cd build
          CC=gcc-14 CXX=g++-14 cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DLINKER=mold \
            -DTESTS=ON -DCOVERAGE=OFF -DWITH_LIGHTNODE=ON -DWITH_CPPSDK=ON -DWITH_TIKV=OFF \
            -DWITH_TARS_SERVICES=OFF -DTOOL=OFF -DVCPKG_TARGET_TRIPLET=x64-linux-release ..
          cmake --build . --target test-bcos-engine opstack-executor-block-tests \
            opstack-executor-tests bcos-evm-opstack-tests test-bcos-ledger -j 4
      - name: C++ fork suites (one invocation per suite; Boost rejects comma lists)
        run: |
          set -euo pipefail
          check_suite() {  # <binary> <suite>
            out=$("$1" --run_test="$2" --log_level=test_suite 2>&1)
            printf '%s\n' "$out"
            echo "$out" | grep -qE 'Running [1-9][0-9]* test cases' \
              || { echo "SUITE DID NOT RUN: $2 on $1"; exit 1; }
            echo "$out" | grep -q '\*\*\* No errors detected'
          }
          check_suite ./build/engine/test/test-bcos-engine OpEnginePayloadShapeBaselineSuite
          check_suite ./build/engine/test/test-bcos-engine OpForkBoundarySweepSuite
          check_suite ./build/engine/test/test-bcos-engine OpHeaderFieldSetBaselineSuite
          check_suite ./build/opstack-executor/tests/opstack-executor-block-tests OpGoldenCorpusProvenance
          check_suite ./build/bcos-evm/test/bcos-evm-opstack-tests OpPrecompilesSuite
          check_suite ./build/bcos-evm/test/bcos-evm-opstack-tests OpOsakaSemanticsSuite
      - name: Set up Python 3.11 (pytest runner for the sweep)
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      - name: Ensure zstd is present
        # The sweep needs the zstd CLI; a missing one must fail HERE (loudly) rather than
        # skip inside pytest. Runners normally ship it — pin it explicitly (review F3).
        run: command -v zstd >/dev/null || sudo apt-get install -y zstd
      - name: M5 registry sweep (zip from the op-geth pin tree; empty/missing fails)
        env:
          OP_GETH_ZIP: ${{ runner.temp }}/op-geth/superchain/superchain-configs.zip
          OP_REQUIRE_REGISTRY_ZIP: "1"
        run: python -m pytest tools/opstack-genesis/test_gen_official_genesis.py -q
      - name: route failure to an issue, do not block PRs
        if: failure()
        run: |
          gh issue create --label test-matrix \
            --title "nightly fork matrix failed: ${{ github.run_id }}" \
            --body "See ${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}"
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

**前置一次性动作**：fork 上没有 `test-matrix` 标签（实测 `gh label list` 0 命中），`gh issue create --label` 会对不存在的标签直接失败。合入后在默认分支执行一次：

```bash
gh label create test-matrix --repo ywy2090/FISCO-BCOS \
  --description "nightly/weekly fork-matrix failures" --color D93F0B
```

- [ ] **Step 5.2: 写 weekly（同构）**

Create `.github/workflows/opstack-fork-weekly.yml`：与 nightly 相同的 setup/build 段（**含 5 个构建 target 与 `GH_TOKEN` 的 issue 路由步**），差异仅：

```yaml
name: opstack-fork-weekly
on:
  schedule:
    - cron: "0 4 * * 0"   # 04:00 UTC, Sundays
  workflow_dispatch:
```

末两步替换为：

```yaml
      - name: Mutation harness (full variant set; restores source, leaves mutant BINARIES — runner is ephemeral)
        run: bash tools/mutation/run.sh
      - name: conformance status placeholder (Plan D not started)
        run: echo "conformance differential execution: pending Plan D; tracked in docs/plans"
```

- [ ] **Step 5.3: 本地空跑守卫反例（dispatch 的替代验收，不能省）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/merge-318-rehearsal
# 6 个 (binary, suite) 组合按 YAML 原样本地跑一遍（绿）
set -euo pipefail
check_suite() { out=$("$1" --run_test="$2" --log_level=test_suite 2>&1);
  echo "$out" | grep -qE 'Running [1-9][0-9]* test cases' || { echo "SUITE DID NOT RUN: $2"; exit 1; }
  echo "$out" | grep -q '\*\*\* No errors detected'; }
check_suite ./build/engine/test/test-bcos-engine OpEnginePayloadShapeBaselineSuite
check_suite ./build/engine/test/test-bcos-engine OpForkBoundarySweepSuite
check_suite ./build/engine/test/test-bcos-engine OpHeaderFieldSetBaselineSuite
check_suite ./build/opstack-executor/tests/opstack-executor-block-tests OpGoldenCorpusProvenance
check_suite ./build/bcos-evm/test/bcos-evm-opstack-tests OpPrecompilesSuite
check_suite ./build/bcos-evm/test/bcos-evm-opstack-tests OpOsakaSemanticsSuite
echo "all suites really ran"
# 反例：故意写错的 suite 名 → 必须命中 SUITE DID NOT RUN（rc=1）
if check_suite ./build/engine/test/test-bcos-engine OpNoSuchSuite; then
  echo "GUARD BROKEN: a wrong suite name passed"; exit 1
else
  echo "empty-run guard confirmed red on a wrong suite name"
fi
```

Expected: 先 `all suites really ran`，再 `empty-run guard confirmed red ...`。

- [ ] **Step 5.4: YAML 校验 + 提交**

```bash
python3 - <<'PY'
import yaml
for p in [".github/workflows/opstack-fork-nightly.yml", ".github/workflows/opstack-fork-weekly.yml"]:
    yaml.safe_load(open(p)); print(p, "valid YAML")
PY
git add .github/workflows/opstack-fork-nightly.yml .github/workflows/opstack-fork-weekly.yml
git commit -m "ci: add nightly and weekly fork-matrix workflows with empty-run guards (WI-12)"
```

- [ ] **Step 5.5: dispatch 验收（待合入，不遗忘）**

合入默认分支后：`gh workflow run opstack-fork-nightly.yml && gh run watch` 全绿；再故意把一个 suite 名改错 dispatch 一次 → 必须红。**本轮**在汇报里列为「待合入验收」。

---

## 验收对照（计划 §验收，逐条）

| # | 原验收 | 本计划的落点 |
|---|---|---|
| 1 | corpus regen 生成 9 envelope 且幂等、契约一致 | **已完成**（C1，语料 `a908a6d0`：rc=0、`0 stale`、shasum 9/9） |
| 2 | `OP_GETH_ZIP` 可配置；`OP_REQUIRE_REGISTRY_ZIP=1` 缺 zip 必 FAIL；扫 0 条必 FAIL | Task 1（`assert chains` 已在 + 新 REQUIRE 门） |
| 3 | nightly/weekly 存在并可手动 dispatch；空跑守卫错 suite 名必红 | Task 5（本地反例代替 dispatch；dispatch 待合入） |
| 4 | header RLP 字段集逐档基线（含顺序），差异逐条定性 | Task 2（**重编码逐字节等价**为决定半（顺序/存在性/取值一次钉住）+ 解码后可选集的不变性断言；`rebuildOpEthHeader:406-470` 门控与 op-geth 映射表为期望来源） |
| 5 | M5 的 Karst 口径二选一写清；Lagoon/Interop 处置 | Task 3（选项 a 落地；Lagoon 按计划自身"更正一"删除并记档——修正验收行 :403 的过时措辞"改为受建模"） |
| 6 | `target-counts.txt` 30 行齐备；PR gate 增量实测 | Task 4（**实测基线改为 40 行**：设计 §6.9 的"30"已过时，见审查 F5） |
| 7 | 未使用目录级 `git add`；过程文档未入库 | 全计划纪律；计数表/impl doc 均 docs/ 下 untracked |

## 外部耦合核查（审查 F8）

spec §0.3 风险表第 1 条要求「升语料 pin 必须同一 commit 删 `matrix/known_deviations.json` 的 karst 条目，否则 matrix 失败」。**已核查该耦合对本轮不成立**：那条偏离的 `expected_from_pin = engine_getPayloadV4` 来自 **op-node** pin dump 出的 `matrix/engine_api_windows.json`（op-node pin `76e4fad5…` 未变），与语料仓 ref 无关；且 `fda7d2b48` 的 bump 之后全量 regen 的 matrix 契约零漂移（判据 3 绿）。**将来若升 op-node pin**，才需要同 commit 处理该条目——写在这里以免后续 agent 重查。

## 对 spec 原件的勘误（`karst-on-5550` 只读，不改原件）

| # | spec 原文 | 实测 | 处置 |
|---|---|---|---|
| 1 | C4 Task 表：「Ecotone 19 字段」 | 20（`+blobGasUsed`、`+excessBlobGas`、`parentBeaconBlockRoot` 三个都在，含 0x80 present-zero） | 以本计划为准（可选项集表述） |
| 2 | C4 补齐：「116 个 golden」（110 + 18 中有该键者） | 顶层 **112** + chained 6 个 `*.golden.json` = 118 个带 `encodedHeaderHex` | Task 0/2 按 118 |
| 3 | C4 补齐：「`OpEngineService.cpp:369-433`」 | `rebuildOpEthHeader` 定义在 **:406**、函数体至 **:470** | 本计划已钉正确范围 |
| 4 | C4 Step 3：「`git add engine/test/CMakeLists.txt`」 | engine 是 `file(GLOB_RECURSE)`，重配即可，无需改 CMakeLists | 作废该 add |
| 5 | C5 验收行：「Lagoon/Interop 格从『容忍』改为『受建模』」 | zip 实测 `lagoon_time` **0 命中**；生成器 `_ROLLUP_FORK_KEYS` 无 lagoon | 按 C5 自带「更正一」删除 Lagoon 项（用户已批准） |
| 6 | §6.9/验收：「target-counts.txt 30 行」 | 实测 40 个测试二进制 | 按实测 40 行（审查 F5） |
| 7 | C3 YAML：5 个 suite 全挂 `test-bcos-engine` | 实际分属 3 个 binary | Task 5 按实测映射重写 |
| 8 | C4 补齐的「三处 pin 不一致」候选问题 | 三处实为不同事物：`GoldenSample.h:22-23` 声明的是 **op-geth pin**（正确）、语料 `.t8n-pin` 是**语料自身 ref** 的标记（滞后属设计内，门只 `::warning::`）、本机 `/Users/octopus/octo/code/op-geth` 是无关 checkout | 不立 finding；只留卫生项：下次语料提交顺手把 `.t8n-pin` 指向 `a908a6d`（消掉永久告警） |

## 执行纪律（每个 Task 都适用）

- 先红后绿；每条新守卫留变异反例的原始输出（C2 的 REQUIRE 门、C4 的重编码变异、C5 的合成 zip、C3 的错 suite 名）。
- 用例数只增不减：受影响 binary 改动前后各报一次 `Running N test cases`。
- 禁止为了让测试变绿改期望表；发现真实偏离 → 登记 `opstack-executor/tests/da-matrix/DIVERGENCES.md`（`^###` 小节）+ 可核实依据。
- 构建串行；engine 改动后必重配；`docs/**`/`.agents/**` 不入库；显式路径 `git add`。
- 完成后推 fork `feat/karst-on-318-merged`（已授权），汇报注明各提交 sha 与「待合入」项。

---

## 执行汇报（2026-09-13，subagent-driven 全流程完成）

基线 → 交付：engine 350→**351**、opstack-executor-block-tests **146**（不变）、bcos-evm-opstack-tests **183**（不变）、pytest 29→**31**。全量计数见 `docs/plans/2026-09-12-target-counts.md`（untracked）。

| Task | 结果 | 提交（本地→已推 fork） |
|---|---|---|
| Task 1 (C2/WI-11) | ✅ zip 路径 env 化 + REQUIRE 必败门 + 4 处 skip 处置 + fail-open 加固 | `f15f90edc` + `7fcd9355e` ✅已推 |
| Task 2 (C4/WI-13) | ✅ 118 golden 重编码逐字节等价 + 可选集不变式（八档表与实测真值逐格吻合）；变异 118/118 红 | `5190dc1c1` + `322bcf804` ✅已推 |
| CI 修复（计划外） | ✅ 推送后 CI 抓出 gcc-14 `-Werror=missing-field-initializers` @ `OpEngineService.inl:1213`（早期提交引入，本地 clang 不报）+ macos 腿 optimism 浅取 `not our ref` 抖动 | `7d378a14c`（engine）✅已推；语料 `c6365ad` + FISCO bump `cc185536a` ✅双端已推 |
| Task 3 (C5/WI-14) | ✅ pre-Karst 口径守卫 + 常驻合成反例；无 zip 环境 skip/REQUIRE fail 语义齐 | `8450b5e19` + `2d363fd52` ✅已推 |
| Task 4 (C6/WI-21) | ✅ 40/40 计数表（3751 用例）；PR gate 增量 ≈1-2min + 变异门未实测（估 5-19min，超限判据"可能但未证"） | 零提交（untracked 文档） |
| Task 5 (C3/WI-12) | ✅ nightly/weekly + 本地空跑反例（错 suite 名必红；新 `check_suite` 输出不再被吞） | `104318bfd` + `daf45bafd` ✅已推 |

流程：每任务实现→spec 合规审查→质量审查→修复→定向复审（Task 1/2/3/5 均走完整循环；终审 READY TO PUSH）。质量审查抓到的关键洞：pre-Ecotone blob 对无禁用侧断言（Task 2）、守卫缺 zip preflight（Task 3）、nightly 缺 gcc-14/mold 与 pytest 安装步 + 路由标签不存在 + 失败输出被吞（Task 5——两条必然首跑失败）。

执行期对计划的修正（均已实证）：①守卫正则单复数（`1 test case` 会假红）；②pytest 基线实为 29 passed（60/59/1 是链计数非用例数）；③engine 基线实为 350（336 系误抄）；④weekly 占位行非法 YAML（裸 `: `）；⑤`test-matrix` 标签在 fork 不存在（现由 workflow 自举）。语料侧新增 `c6365ad`（fetch 重试），FISCO 侧 ref bump `cc185536a`。

新登记：`test-transaction-executor` 的 `TestHostContext/nestConstructor`（180 例中 2 检查失败）——上游继承（分支 0 提交触该路径），建议与 F-TEST 系列并册。

验收清单：①C1 已完成（语料 a908a6d/c6365ad 双远端）✅；②OP_GETH_ZIP 可配置 + REQUIRE 必败 + 空扫必败 ✅；③nightly/weekly 存在、本地空跑守卫红证 ✅、dispatch **待合入** ⏸；④header 基线逐档落位（含顺序，byte 级）✅；⑤Karst 口径=选项 a 写清、Lagoon 删除有据 ✅；⑥计数表 40 行 + 预算实测（变异门留待 CI 实数）✅；⑦无目录级 add、过程文档未入库 ✅。

待办（合入默认分支后）：`gh label create test-matrix`（亦可依赖 workflow 自举）→ `gh workflow run opstack-fork-nightly.yml` 全绿 → 故意错 suite 名再跑必红。CI 观察：推送后 run `34744115405` 已触发、ci_pins 门 success，构建腿进行中（本报告落盘时未完）。

**fisco-review Round 1 @ `daf45bafd`（追加，2026-09-13）**：1 HIGH——nightly/weekly 的 regen 复合 action 在调用方工作区找 `regen.sh`，而 F-CI-2 已解除跟踪且两文件无 corpus checkout/symlink ⇒ 首跑必死（三轮任务级审查均未升级此顾虑，Task 5 报告里只是"待 dispatch 确认"）。**Fixed in `30a56a140`**（补 e2e-tests checkout + t8n symlink，已推）。1 LOW（get-cmake 浮动 ref）记为接受。报告：`.agents/reviews/plan-c-daf45bafd/report-round1.md`。

**CI 攻坚（-Werror=missing-field-initializers 类，2026-09-13）**：推送后 CI 逐轮暴露该类硬错误（gcc-14 `-Werror`，本地 clang 默认不查）。修复序列：`7d378a14c`（`OpEngineService.inl`）→ `82077aafa`（clang 语法清扫 11 站 + EthTransitionTest 字段序纠正）→ `30a56a140` 之后的 `f5346e39a`（第二批：`Op7702Test`×2、`OpOsakaSemantics`、`OpTransitionTest`×7、`RollupCostTest`、`OpValidateTest`×3 的 `OpFeeParams`，以及 `ImportedStoreTest` 的 7 处 `ImportedBlock`）。**方法论教训**：①`clang -Wmissing-designated-field-initializers` 可复现 gcc 该告警，但**必须逐文件跑**——unity TU 会因无关错误提前中止，把"没报"误当"干净"（我第一版清扫因此漏检 24 站）；②clang 每个初始化只报**第一个**缺失字段，故补全要按结构补全**全部**无 NSDMI 成员、且严格按声明序（否则 `-Wreorder-init-list`）；③本地无真 gcc 可用（Homebrew gcc-14 与 Apple SDK 冲突），口径只能靠 clang 近似；④`git checkout` 恢复被脚本污染的 `RollupCostTest`（把 `x.field = ...;` 赋值行误当初始化锚点，孤插 6 行）后改为花括号块内解析；⑤pre-commit 的 clang-format 门会拦下未格式化的插入行。残余未验面：CI 的 L2 job（`WITH_L2_CONTRACTS=ON`）源码不在本地 configure 内，其 TU 未被清扫覆盖。

**本地校验该告警类的配方（2026-09-13 定型，回答"为何一直在修/能否本地校验"）**：根因是编译器差异——`-Wmissing-field-initializers` **在 GCC 属于 `-Wextra`、在 clang 不属于**，而本仓 `cmake/CompilerSettings.cmake:34` 只有 `-Werror -Wall -Wextra`；故同一份代码本地（Apple clang）绿、CI（gcc-14）红。可复现的本地门：

```bash
# 1) 造一个带该告警的镜像构建目录（复用已装的 vcpkg，只多花 ~2.5min 配置）
cmake -B build-l2 -S . -DCMAKE_CXX_FLAGS="-Wmissing-field-initializers" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_BUILD_TYPE=MinSizeRel -DTESTS=ON -DWITH_L2_CONTRACTS=ON -DWITH_LIGHTHOUSE=ON 2>/dev/null || true
cmake -B build-l2 -S . -DCMAKE_CXX_FLAGS="-Wmissing-field-initializers" -DTESTS=ON -DWITH_L2_CONTRACTS=ON \
  -DWITH_LIGHTNODE=ON -DWITH_CPPSDK=ON -DWITH_TIKV=OFF -DWITH_TARS_SERVICES=OFF -DTOOLS=OFF \
  -DVCPKG_INSTALLED_DIR="$PWD/build/vcpkg_installed" -DVCPKG_TARGET_TRIPLET=arm64-osx
# 2) 必须**显式构建受影响的 target**（"全量 make" 未必编到它们；unity 下更要注意）
make -C build-l2 -k -j6 bcos-evm-opstack-tests test-bcos-engine opstack-executor-block-tests
# grep 'error: missing initializer' 期望 0
```

教训（三条，都是踩过的）：①**clang 单文件扫描的"没报"不等于干净**——unity TU 会因无关错误提前中止，成员文件从未被检查（我第一版清扫因此漏 24 站）；②**clang 每个初始化只报第一个缺失字段**，自动化补全必须按结构补齐全部无 NSDMI 成员且严格按声明序（否则 `-Wreorder-init-list`）；③**脚本改写不可省验证**——我那版插入器把 `x.field = ...;` 赋值行误当锚点（污染 `RollupCostTest` 6 行，已 `git checkout` 重做），又在块终止行同时含 `.operator_fee_constant` 时静默漏插（正是 CI 最后一处 `:181`）。

**CI 攻坚续（2026-09-13/14 深夜）**：run `34796622934`（b9402adc1）拿到**多项首次**——Build 步过（-Werror 类清零得证）、**Regen 步过**（op-node fork-fetch 生效）、**Test 步全量 ctest 首次绿**、Fork-label 守卫过、变异门 9 变体全部 RED-as-required；最后倒在变体补丁三方合并（`fetch-depth=1` 缺基线 blob）。修复 `d847cba19`：变异门前 `git fetch --deepen=400`（仅浅克隆时）。**方法论**：CI 的 job 内步骤顺序决定"过了某步"的含义——build job 里 regen 在 Build **之后**，此前"ubuntu 过了 regen"系误读；op-node pin `76e4fad5`（本地提交"Add code comment"）**从不在上游任何 ref 上**，已在 fork 建分支 `op-node-pin-76e4fad5`（公开匿名可读）并令语料默认从 fork 取（`7cd99b8`，`OP_NODE_FETCH_URL` 可覆盖）。
