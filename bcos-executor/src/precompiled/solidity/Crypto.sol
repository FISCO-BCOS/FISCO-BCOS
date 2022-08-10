// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

abstract contract Crypto
{
    function sm3(bytes memory data) public view returns(bytes32){}
    function keccak256Hash(bytes memory data) public view returns(bytes32){}
    function sm2Verify(bytes32 message, bytes memory publicKey, bytes32 r, bytes32 s) public view returns(bool, address){}
    function curve25519VRFVerify(bytes memory message, bytes memory publicKey, bytes memory proof) public view returns(bool, uint256){}
}
