// SPDX-License-Identifier: MIT
pragma solidity ^0.8.20;

/// Harnesses for the eth_estimateGas exactness test.
///
/// Every variant is state-idempotent: the storage writes always store the same value that is
/// already there, so an SSTORE costs the same (warm ASSIGNED) on every run. That matters
/// because the test compares the estimate against a real transaction sent immediately after;
/// if the first run changed the slot, the second would see a different SSTORE cost and the
/// comparison would be meaningless.
///
/// Recursion forwards all remaining gas (plain `address(this).call`), so EIP-150's 63/64 rule
/// binds at every level -- the situation the estimator exists for.
contract GasHarness {
    uint256 public sink = 1;
    uint256 public sink2 = 1;

    /// mode 0: no storage write
    /// mode 1: idempotent SSTORE before the child call, at every level
    /// mode 2: idempotent SSTORE only in the leaf frame
    /// mode 3: idempotent SSTORE after the child call returns, at every level
    function rec(uint256 depth, uint256 rounds, uint256 mode) public returns (uint256) {
        if (mode == 1) {
            sink = 1;
        }
        uint256 acc = burn(rounds);
        if (depth == 0) {
            if (mode == 2) {
                sink = 1;
            }
            return acc;
        }
        (bool ok, bytes memory ret) = address(this).call(
            abi.encodeWithSelector(this.rec.selector, depth - 1, rounds, mode)
        );
        require(ok, "child failed");
        if (mode == 3) {
            sink = 1;
        }
        return acc ^ abi.decode(ret, (uint256));
    }

    /// Two children per level: exercises "call sites take the maximum, never the sum" and
    /// the fact that a frame keeps its remaining gas after the first child returns.
    function fan(uint256 depth, uint256 rounds, uint256 mode) public returns (uint256) {
        if (mode == 1) {
            sink = 1;
        }
        uint256 acc = burn(rounds);
        if (depth == 0) {
            return acc;
        }
        (bool ok1, bytes memory r1) = address(this).call(
            abi.encodeWithSelector(this.fan.selector, depth - 1, rounds, mode)
        );
        (bool ok2, bytes memory r2) = address(this).call(
            abi.encodeWithSelector(this.fan.selector, depth - 1, rounds, mode)
        );
        require(ok1 && ok2, "child failed");
        return acc ^ abi.decode(r1, (uint256)) ^ abi.decode(r2, (uint256));
    }

    /// Two independent storage slots written at every level, so the sentry surplus has to be
    /// paid once per writing frame rather than once per instruction.
    function twoSlots(uint256 depth, uint256 rounds) public returns (uint256) {
        sink = 1;
        sink2 = 1;
        uint256 acc = burn(rounds);
        if (depth == 0) {
            return acc;
        }
        (bool ok, bytes memory ret) = address(this).call(
            abi.encodeWithSelector(this.twoSlots.selector, depth - 1, rounds)
        );
        require(ok, "child failed");
        return acc ^ abi.decode(ret, (uint256));
    }

    /// Fixed amount of computation that does not depend on the gas budget, so adjusting the
    /// budget cannot change the trace (the premise the estimate relies on).
    function burn(uint256 rounds) internal pure returns (uint256 acc) {
        for (uint256 i = 0; i < rounds; ++i) {
            acc = uint256(keccak256(abi.encode(acc, i)));
        }
    }
}
