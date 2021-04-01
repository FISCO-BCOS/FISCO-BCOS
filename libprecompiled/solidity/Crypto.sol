contract Crypto
{
    function sm3(bytes data) public view returns(bytes32){}
    function keccak256Hash(bytes data) public view returns(bytes32){}
    function sm2Verify(bytes32 message, bytes publicKey, bytes32 r, bytes32 s) public view returns(bool, address){}
    function curve25519VRFVerify(string input, string vrfPublicKey, string vrfProof) public view returns(bool,uint256){}
}