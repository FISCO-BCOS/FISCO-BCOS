pragma solidity ^0.6.0;

import "./precompiled/Crypto.sol";

contract TestCrypto {
    Crypto crypto;
    constructor () public {
        crypto = Crypto(0x100a);
    }
    function sm3(bytes memory data) public view returns (bytes32){
        return crypto.sm3(data);
    }

    function keccak256Hash(bytes memory data) public view returns (bytes32){
        return crypto.keccak256Hash(data);
    }

    function sm2Verify(bytes memory message, bytes memory sign) public view returns (bool, address){
        return crypto.sm2Verify(message, sign);
    }

    function getSha256(bytes memory _memory) public returns(bytes32 result)
    {
        return sha256(_memory);
    }

    function getKeccak256(bytes memory _memory) public returns(bytes32 result)
    {
        return keccak256(_memory);
    }
}