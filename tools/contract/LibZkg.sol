/**
* file LibZkg.sol
* author jimmyshi
* time 2018-03-01
* Print log into eth
*/
pragma solidity ^0.4.2;

import "LibString.sol";
import "LibInt.sol";

library LibZkg {
    using LibString for *;
    using LibInt for *;


    //EthLog的EthCallId定义
    function tx1To1CallId() internal constant returns(uint32) {
        return 0x5A4B47; //一对一匿名可监管交易验证
    }

    function to1To1Verify(
        string inputRt0, string inputRt1,
        string inputSn0, string inputSn1,
        string outputCm0, string outputCm1,
        uint64 vpubOld,
        uint64 vpubNew,
        string g,
        string gpk,
        string gData,
        string proof
        ) 
        internal constant 
        returns(uint)
    {
        
        /*
         *  受到参数个数限制，调用ethcall前要封装
         *  params 中的内容，每个都是64个char
        */
        string memory params = "";
        params = params.concat(inputRt0, inputRt1);
        params = params.concat(inputSn0, inputSn1);
        params = params.concat(outputCm0, outputCm1);
        params = params.concat(g, gpk);
        return callEvm(
            params,
            vpubOld, vpubNew,
            gData,
            proof
        );
    }

    //调用底层的ethcall
    function callEvm(
        string params,
        uint64 vpubOld,
        uint64 vpubNew,
        string gData,
        string proof
        ) 
        internal constant 
        returns(uint)
    {
        uint32 callId = tx1To1CallId();
        uint r;
        assembly{
            r := ethcall(callId, params, 
                vpubOld, vpubNew,
                gData, proof, 0, 0, 0, 0)
        }
        return r;
    }



}

