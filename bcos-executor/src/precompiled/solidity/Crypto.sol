// SPDX-License-Identifier: Apache-2.0
pragma solidity ^0.6.0;

contract Crypto
{
    function sm3(bytes memory data) public view returns(bytes32){}
    function keccak256Hash(bytes memory data) public view returns(bytes32){}
    function sm2Verify(bytes memory message, bytes memory sign) public view returns(bool, address){}
}
