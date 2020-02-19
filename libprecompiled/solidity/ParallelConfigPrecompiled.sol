pragma solidity ^0.4.24;

contract ParallelConfigPrecompiled {
    function registerParallelFunctionInternal(address, string, uint256)
        public
        returns (int256);
    function unregisterParallelFunctionInternal(address, string)
        public
        returns (int256);
}
