// SPDX-License-Identifier: UNLICENSED
pragma solidity ^0.8.0;

contract BalancePrecompiled {
    function getBalance(address account) public view returns (uint256) {}

    function addBalance(address account, uint256 amount) public {}

    function subBalance(address account, uint256 amount) public {}

    function transfer(address from, address to, uint256 amount) public {}

    function registerCaller(address account) public {}

    function unregisterCaller(address account) public {}

    function listCaller() public view returns (address[] memory) {}
}
