# bcos-evm

复用 evmone(官方 `ipsilon/evmone` v0.21.0 + `ports/evmone/fisco-sm3.patch`,
hash_fn 挂 VM)的 ETH + OP Stack 执行参考模块。自 `bcos-evm-ref`
(分支 `feat-evm-mb1-block-execution`,基准 1cec91b27639cab7037bcf344d4109fd19334fff)
全保真移植,底座由 vcpkg 导出 `evmone::state` 改为 vendored 源
(`bcos-evm/eth/state/`、`bcos-evm/eth/utils/`,取自官方 v0.21.0,仅 include 改写)。

- `eth/`:ETH 状态转换内核(EthTransition + vendored state/utils)
- `adapter/`:StateDiff 消毒/严格写回/stateRootOf/StateView 适配
- `opstack/`:OP 薄层(processOpBlock/sealOpBlock/deposit/fee/receipt)
- `test/opstack/`:21 测试文件 124 用例,含 `OpT8nReplay.Vectors`
  块级 op-geth(pinned v1.101702.2)差分 gate(33 向量,ctest 常驻)
- `scripts/upstream-diff.sh`:照抄面静态护栏(EVMONE_GIT 指官方 v0.21.0 检出)

## Build(standalone)

    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake
    cmake --build build
    ctest --test-dir build --output-on-failure

## 边界(继承 ref spec §1.1 R2,E-b park 不解除)

t8n gate 的 `pre` 播种 `InMemoryStateView`,不经真实账本;本模块绿灯
**不构成** OP 路径生产可用或 op-geth 生产等价的宣称依据。M3.5 P2
真账本桥接、Karst 真适配(现仅 Jovian 别名占位)均未做。
向量再生成纪律与 DIVERGENCES 豁免流程见
`test/opstack/t8n/generator/README.md` 与 vectors/DIVERGENCES.md(出处
记录中的 `bcos-evm-ref` 路径为历史原貌,有意保留)。
