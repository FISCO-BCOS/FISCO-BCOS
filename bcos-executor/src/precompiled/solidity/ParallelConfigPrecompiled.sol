// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;

contract ParallelConfigPrecompiled {
    function registerParallelFunctionInternal(address, string memory, uint256)
    public
    returns (int256){}
    function unregisterParallelFunctionInternal(address, string memory)
    public
    returns (int256){}
}
