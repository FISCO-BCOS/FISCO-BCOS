// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/ECRecoverTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.6.0 <0.8.12;

contract ECRecoverTest {
    event Address(address addr);

    function genValidParams() private pure returns(bytes memory){
        bytes memory data = abi.encode(
            bytes32(0xaa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8),
            uint8(28),
            bytes32(0xacd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b),
            bytes32(0x01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4));

        return data;
    }

    function genInvalidParams() private pure returns(bytes memory){
        bytes memory data = abi.encodeWithSignature("ecrecover(bytes32,uint8,bytes32,bytes32)",
            bytes32(0xaa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8),
            uint8(28),
            bytes32(0xacd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b),
            bytes32(0x01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b5));

        return data;
    }

    function genLargerInvalidParams(uint addNum) private pure returns(bytes memory){
        // append genValidParams() addNum byte
        bytes memory addBytes = new bytes(addNum);
        bytes memory data = abi.encodePacked(genValidParams(), addBytes);
        require(genValidParams().length + addNum == data.length, "genLargerInvalidParams failed");

        return data;
    }

    function genShorterInvalidParams(uint cutNum) private pure returns(bytes memory){
        // remove genValidParams() cutNum byte
        bytes memory validParams = genValidParams();
        require(validParams.length >= cutNum, "genShorterInvalidParams failed");
        bytes memory data = new bytes(validParams.length - cutNum);
        for(uint i = 0; i < validParams.length - cutNum; i++){
            data[i] = validParams[i];
        }

        require(genValidParams().length - cutNum == data.length, "genShorterInvalidParams failed");
        return data;
    }

    function callECRecover(bytes memory params) public returns (address){
        // 调用预编译合约的ECRecover函数
        address precompiledContract = address(0x1);
        (bool success, bytes memory result) = precompiledContract.call(params);
        require(success, "Call to precompiled contract failed");

        if (result.length == 0) {
            return address(0);
        }

        address addr;
        assembly {
            addr := mload(add(result, 32))
        }

        return addr;
    }

    function callECRecoverValid() public returns (address){
        bytes memory params = genValidParams();
        return callECRecover(params);
    }

    function callECRecoverInvalid() public returns (address){
        bytes memory params = genInvalidParams();
        return callECRecover(params); // must return 0x0000000000000000000000000000000000000084
    }

    function callECRecoverDirect() public pure returns (address){
        bytes32 hash =  bytes32(0xaa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8);
        uint8 v = uint8(28);
        bytes32 r = bytes32(0xacd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b);
        bytes32 s = bytes32(0x01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4);

        address addr = ecrecover(hash, v, r, s);
        require(addr == address(0x6DA0599583855F1618B380f6782c0c5C25CB96Ec), "ecrecover failed");
        return addr;
    }

    function testRecoverEmptyParams() public {
        bytes memory data = abi.encode();
        address addr = callECRecover(data);
        require(addr == address(0x0), "should recover failed");
    }

    function testRecoverVRange() public {
        uint8[9] memory vs = [uint8(0), uint8(1), uint8(2), uint8(10), uint8(27), uint8(28), uint8(100), uint8(200), uint8(255)];

        for(uint i = 0; i < vs.length; i++){
            uint8 v = vs[i];
            bytes memory data = abi.encode(
                bytes32(0xaa0f7414b7f8648410f9818df3a1f43419d5c30313f430712033937ae57854c8),
                v,
                bytes32(0xacd0d6c91242e514655815073f5f0e9aed671f68a4ed3e3e9d693095779f704b),
                bytes32(0x01932751f4431c3b4c9d6fb1c826d138ee155ea72ac9013d66929f6a265386b4));

            address addr = callECRecover(data);
            // only v = 27 or 28 is valid
            if (v == 27){
                require(addr == address(0xA5b4792dcAD4fE78D13f6Abd7BA1F302945DE4f7), "ecrecover failed");
            } else if (v == 28){
                require(addr == address(0x6DA0599583855F1618B380f6782c0c5C25CB96Ec), "ecrecover failed");
            } else {
                require(addr == address(0x0), "should recover failed");
            }
        }
    }

    function testRecoverLargerParams() public {
        uint[10] memory addNums = [uint(1), uint(5), uint(32), uint(33), uint(64), uint(65), uint(256), uint(257), uint(512), uint(513)];

        for(uint i = 0; i < addNums.length; i++){
            uint addNum = addNums[i];
            bytes memory data = genLargerInvalidParams(addNum);
            address addr = callECRecover(data);
            require(addr == address(0x6DA0599583855F1618B380f6782c0c5C25CB96Ec), "added bytes should be ignored");
        }
    }

    function testRecoverShorterParams() public {
        uint[11] memory cutNums = [uint(1), uint(5), uint(10), uint(18), uint(27), uint(31), uint(32), uint(33), uint(63), uint(64), uint(65)];

        for(uint i = 0; i < cutNums.length; i++){
            uint cutNum = cutNums[i];
            bytes memory data = genShorterInvalidParams(cutNum);
            address addr = callECRecover(data);
            // should failed when cutNum >= 32

            if (cutNum == 1) {
                require(addr == address(0x509eAd8B20064f21E35f920cB0c6d6cBC0C0Aa0d), "should success when cutNum == 1");
            } else if (cutNum == 5) {
                require(addr == address(0x571A110CE923c9354b11B247F087b6dab1aD9089), "should success when cutNum == 5");
            } else if (cutNum == 10) {
                require(addr == address(0xfaCf3C4D9C0197bF621C39D461970e7A5D2F6947), "should success when cutNum == 10");
            } else if (cutNum == 18) {
                require(addr == address(0xCC63021A8A9e4C5c58F275C1DbA8536D398C46F5), "should success when cutNum == 18");
            } else if (cutNum == 27) {
                require(addr == address(0xC6A74652861114A92A30b8399e6EBe2e2e90313E), "should success when cutNum == 27");
            } else if (cutNum == 31) {
                require(addr == address(0xc275caC475391eeEadc7A4d0A09781177776D8B5), "should success when cutNum == 31");
            } else {
                require(addr == address(0x0), "should failed when cutNum >= 32");
            }

        }
    }

    function testRecoverBasic() public {
        require(callECRecoverValid() == address(0x6DA0599583855F1618B380f6782c0c5C25CB96Ec), "callECRecoverValid failed");
        require(callECRecoverDirect() == address(0x6DA0599583855F1618B380f6782c0c5C25CB96Ec), "callECRecoverDirect failed");
    }

    function testEmitEvent() public {
        emit Address(address(0x6DA0599583855F1618B380f6782c0c5C25CB96Ec));
        callECRecoverDirect();
    }

    function check() public {
        testRecoverBasic();
        testRecoverEmptyParams();
        testRecoverVRange();
        testRecoverLargerParams();
        testRecoverShorterParams();
    }
}