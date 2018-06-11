pragma solidity ^0.4.2;

import "LibZkg.sol";

contract TestZkg {
    uint res;

    event hasBeenCalled();

    function transaction(
        string inputRt0, string inputRt1,
        string inputSn0, string inputSn1,
        string outputCm0, string outputCm1,
        uint64 vpubOld,
        uint64 vpubNew,
        string g,
        string gpk,
        string gData,
        string proof
    ) {
        hasBeenCalled();
        res = LibZkg.to1To1Verify(
            inputRt0, inputRt1,
            inputSn0, inputSn1,
            outputCm0, outputCm1,
            vpubOld,
            vpubNew,
            g,
            gpk,
            gData,
            proof
        );
    }

    function get()constant returns(uint){
        return res;
    }
}
