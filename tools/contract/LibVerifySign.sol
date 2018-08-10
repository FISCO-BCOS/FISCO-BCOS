pragma solidity ^0.4.2;

library LibVerifySign {

    function callId() internal constant returns(uint32) {
        return 0x66669;
    }
    
    //验证交易的合法性
    function verifySign(string hash, string pubs, string signs, string idxs) internal constant returns (uint) {
        uint32 call_id = callId();
        uint r;
        assembly{
            r := ethcall(call_id, hash, pubs, signs, idxs, 0, 0, 0, 0, 0)
        }
    
        return r;
    }
}
