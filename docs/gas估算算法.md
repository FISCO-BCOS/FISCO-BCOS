# 基于一次 EVM 模拟执行的最低 Gas 预算计算

## 1. 目标与适用范围

通过一次模拟执行记录调用树，并自底向上计算每个调用帧的最低初始 Gas 预算，避免反复调整 Gas limit 并重新执行。

本方法计算的是：**在执行轨迹和各段实际消耗不变的前提下，满足总消耗及 EIP-150 转发限制所需的最低预算。**

基础模型假设：

- 调整初始 Gas 不会改变执行路径和实际消耗；
- 子调用请求的转发 Gas 足够大，不受额外的显式上限限制；
- 暂不涉及 value stipend、异常耗尽 Gas、特殊剩余 Gas 门槛等情况。

因此，该方法不是任意 EVM 合约的全局最小可成功 Gas 算法。

## 2. 核心思想

每个调用帧需要区分两个量：

- `used`：该帧及其后代实际消耗的 Gas；
- `required`：该帧完成执行所需的最低初始 Gas。

由于 EIP-150 的 63/64 转发限制，`required` 可能大于 `used`。

父帧计算后续剩余预算时，使用子帧的实际消耗；检查能否向子帧转发足够预算时，使用子帧的最低需求。

所有计算均以调用帧为单位，不能使用一个全局累计消耗替代各帧的局部记账，因为祖先帧保留的 Gas 无法被当前帧直接使用。

## 3. 参数定义

对于调用帧 A，定义：

- $X_A$：进入 A 时的初始 Gas；
- $u_A$：A 及其所有后代的实际总消耗；
- $P_i$：从 A 入口到第 $i$ 次子调用分配 Gas 之前，累计消耗的 Gas；
- $r_i$：第 $i$ 个子调用及其后代所需的最低初始 Gas。

$P_i$ 的取值位置必须满足：

> 已扣除本次调用的基础费用、内存扩展等转发前开销，但尚未扣除转发给子调用的 Gas。

因此，第 $i$ 次调用分配前的剩余预算为：

$$
L_i=X_A-P_i
$$

$r_i$ 不包括该子调用返回后父帧自身的后续执行。

## 4. 63/64 限制的逆函数

EIP-150 规定，父帧分配前剩余 $L$ Gas 时，最多可转发：

$$
f(L)=L-\left\lfloor\frac{L}{64}\right\rfloor
$$

为了让子调用获得至少 $r$ Gas，父帧此时最低需要：

$$
h(r)=
\begin{cases}
0, & r=0\\[4pt]
r+\left\lfloor\frac{r-1}{63}\right\rfloor, & r>0
\end{cases}
$$

这是满足 $f(L)\ge r$ 的最小整数解。

不能直接用 $\lceil64r/63\rceil$ 替代并称为精确结果，因为 EVM 的整数取整方式可能使该近似值多算 1 Gas。

## 5. 调用帧递推公式

对于每个子调用，必须满足：

$$
X_A\ge P_i+h(r_i)
$$

同时，A 必须具有足够预算覆盖自身及所有后代的实际总消耗：

$$
X_A\ge u_A
$$

因此：

$$
\boxed{
r_A=
\max\left(
u_A,\;
\max_i[P_i+h(r_i)]
\right)
}
$$

对于没有子调用的叶子帧，在基础模型下：

$$
r_A=u_A
$$

不同调用点的预算要求取最大值，而不是相加，因为父帧保留的 Gas 可以在后续继续使用。

与 $u_A$ 取最大值不能省略，否则可能漏掉最后一次子调用返回后的收尾开销。

## 6. 栈式实现

每个正在执行的调用帧维护：

    Frame {
        entryGas       // 模拟执行时的入口 Gas
        maxRequired    // 已处理调用点要求的最大初始预算
        pendingPrefix  // 当前子调用分配前的累计消耗 P_i
    }

处理流程：

    进入调用帧 A：
        压栈
        A.entryGas = 输入 Gas
        A.maxRequired = 0

    A 即将调用 B：
        在扣完调用开销、转发 Gas 之前记录父帧剩余 Gas
        A.pendingPrefix = A.entryGas - remainingGasBeforeForwarding

        执行 B，压入 B 的调用帧

    B 执行结束：
        usedB = B.entryGas - B.gasLeft
        requiredB = max(B.maxRequired, usedB)
        弹出 B

        A.maxRequired = max(
            A.maxRequired,
            A.pendingPrefix + h(requiredB)
        )

    A 执行结束：
        usedA = A.entryGas - A.gasLeft
        requiredA = max(A.maxRequired, usedA)
        弹出 A

        将 requiredA 返回给父帧

算法在一次模拟执行中采集信息，沿调用栈自底向上完成计算。

额外的递推计算时间为 O(N)，栈空间为 O(D)，其中 N 为调用帧数量，D 为最大调用深度；不包含模拟执行本身的成本。

## 7. Host / evmone 接入注意事项

仅依赖 `host.call` 接收到的子调用 Gas 和返回的 `gas_left`，通常不足以计算父帧的 $P_i$。

需要在 evmone 内部对应位置增加追踪，或提供等价信息，取得：

- 父帧入口 Gas；
- 扣完调用费用、尚未转发 Gas 时的父帧剩余 Gas。

不要根据子调用的转发 Gas 反推父帧剩余 Gas，尤其是在存在显式转发上限时。

另外：

- 父帧的 `entryGas - gasLeft` 已包含后代消耗，不能再重复累加后代的总消耗；
- `used` 必须是执行期间、交易退款结算前的实际消耗，不能直接用交易回执的最终计费 `gasUsed` 替代。

## 8. 从入口预算到交易 Gas limit

递推得到的根帧 `required` 是入口合约的执行预算，不是完整交易的 Gas limit。

在仅考虑固有费用与执行预算的模型下：

$$
G_{\text{limit,min}}
=
G_{\text{intrinsic}}+r_{\text{root}}
$$

完整实现还需处理适用的交易级最低 Gas 要求、Gas 上限和其他协议规则。交易结束时的退款不能用于降低执行期间必须具备的预算。

## 9. 尚需单独处理的情况

以下情况超出上述基础递推公式：

- `gasleft()` 等导致执行路径或消耗随预算变化；
- 显式 `call{gas: k}` 或动态计算的转发限制；
- 带 value 的调用 stipend；
- `SSTORE` 等特殊剩余 Gas 门槛；
- 子调用异常耗尽预算，或父帧依赖子调用失败继续执行；
- 合约创建等需要额外建模的执行和结算规则。

总结：该方法的核心是逐帧计算
“前缀消耗 + 子调用最低预算的逆 63/64 变换”，
对所有调用点取最大值，再与该帧实际总消耗取最大值，并将结果向父帧传播。