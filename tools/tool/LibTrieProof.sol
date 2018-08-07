pragma solidity ^0.4.2;

library LibTrieProof {

    function callId() internal constant returns(uint32) {
        return 0x66668;
    }
    
    //验证交易的合法性
    function verifyProof(string root, string proofs, string key, string targetValue) internal constant returns (uint) {
        uint32 call_id = callId();
        uint r;
        assembly{
            r := ethcall(call_id, root, proofs, key, targetValue, 0, 0, 0, 0, 0)
        }
    
        return r;
    }
}
