pragma solidity ^0.4.2;

library LibPaillier {
    
    function callId() internal constant returns(uint32)
    {
        return 0x20;
    } 

    function paillierAdd(string d1, string d2) 
        internal constant 
        returns (string) 
    {
        string memory ret = new string(bytes(d1).length);
        uint32 call_id = callId();
        uint r;
        assembly{
            r := ethcall(call_id, ret, d1, d2, 0, 0, 0, 0, 0, 0)
        }
  
        return ret;
    }
}
