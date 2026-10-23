// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/Cast.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

abstract contract Cast {
    function stringToS256(string memory) public virtual view returns (int256);
    function stringToS64(string memory) public virtual view returns (int64);
    function stringToU256(string memory) public virtual view returns (uint256);
    function stringToAddr(string memory) public virtual view returns (address);
    function stringToBytes32(string memory) public virtual view returns (bytes32);
    
    function s256ToString(int256) public virtual view returns (string memory);
    function s64ToString(int64) public virtual view returns (string memory);
    function u256ToString(uint256) public virtual view returns (string memory);
    function addrToString(address) public virtual view returns (string memory);
    function bytes32ToString(bytes32) public virtual view returns (string memory);
}