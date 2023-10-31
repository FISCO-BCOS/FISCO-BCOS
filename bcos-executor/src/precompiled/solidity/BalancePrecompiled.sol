// SPDX-License-Identifier: UNLICENSED
pragma solidity ^0.8.0;

contract BalancePrecompiled {
    function getBalance(address account) external returns (uint256) {}

    function addBalance(address account, uint256 amount) external {}

    function subBalance(address account, uint256 amount) external {}

    function transfer(address from, address to, uint256 amount) external {}

    function registerCaller(address account) external {}

    function unregisterCaller(address account) external {}
}
