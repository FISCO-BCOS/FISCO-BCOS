// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;

contract CNSPrecompiled
{
    function insert(string memory name, string memory version, address addr, string memory abiStr) public returns(uint256){}
    function selectByName(string memory name) public view returns(string memory){}
    function selectByNameAndVersion(string memory name, string memory version) public view returns(address,string memory){}
    function getContractAddress(string memory name, string memory version) public view returns(address){}
}

