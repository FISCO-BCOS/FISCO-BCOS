// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;

contract TestEvmPrecompiled {
    event callGasUsed(uint256);

    function keccak256Test(bytes memory b) public returns (bytes32 result) {
        uint256 gasLeft1 = gasleft();
        result = keccak256(b);
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed > 0, "gasleft check error");
    }

    // 0x01
    function ecRecoverTest(
        bytes32 _hash,
        uint8 _v,
        bytes32 _r,
        bytes32 _s
    ) public returns (address pub) {
        uint256 gasLeft1 = gasleft();
        pub = ecrecover(_hash, _v, _r, _s);
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 3000, "gasleft check error");
    }

    // 0x02
    function sha256Test(bytes memory b) public returns (bytes32 result) {
        uint256 gasLeft1 = gasleft();
        result = sha256(b);
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 60, "gasleft check error");
    }

    // 0x03
    function ripemd160Test(bytes memory b) public returns (bytes32 result) {
        uint256 gasLeft1 = gasleft();
        result = ripemd160(b);
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 600, "gasleft check error");
    }

    // 0x04
    function callDatacopy(bytes memory data) public returns (bytes memory) {
        bytes memory ret = new bytes(data.length);
        uint256 gasLeft1 = gasleft();
        assembly {
            let len := mload(data)
            if iszero(
            call(gas(), 0x04, 0, add(data, 0x20), len, add(ret, 0x20), len)
            ) {
                invalid()
            }
        }
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 15, "gasleft check error");
        return ret;
    }

    // 0x05
    function callBigModExp(
        bytes32 base,
        bytes32 exponent,
        bytes32 modulus
    ) public returns (bytes32 result) {
        uint256 gasLeft1 = gasleft();
        assembly {
        // free memory pointer
            let memPtr := mload(0x40)

        // length of base, exponent, modulus
            mstore(memPtr, 0x20)
            mstore(add(memPtr, 0x20), 0x20)
            mstore(add(memPtr, 0x40), 0x20)

        // assign base, exponent, modulus
            mstore(add(memPtr, 0x60), base)
            mstore(add(memPtr, 0x80), exponent)
            mstore(add(memPtr, 0xa0), modulus)

        // call the precompiled contract BigModExp (0x05)
            let success := call(gas(), 0x05, 0x0, memPtr, 0xc0, memPtr, 0x20)
            switch success
            case 0 {
                revert(0x0, 0x0)
            }
            default {
                result := mload(memPtr)
            }
        }
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 100000, "gasleft check error");
    }

    // 0x06
    function callBn256Add(
        bytes32 ax,
        bytes32 ay,
        bytes32 bx,
        bytes32 by
    ) public returns (bytes32[2] memory result) {
        bytes32[4] memory input;
        input[0] = ax;
        input[1] = ay;
        input[2] = bx;
        input[3] = by;
        uint256 gasLeft1 = gasleft();
        assembly {
            let success := call(gas(), 0x06, 0, input, 0x80, result, 0x40)
            switch success
            case 0 {
                revert(0, 0)
            }
        }
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 150, "gasleft check error");
    }

    // 0x07
    function callBn256ScalarMul(
        bytes32 x,
        bytes32 y,
        bytes32 scalar
    ) public returns (bytes32[2] memory result) {
        bytes32[3] memory input;
        input[0] = x;
        input[1] = y;
        input[2] = scalar;
        uint256 gasLeft1 = gasleft();
        assembly {
            let success := call(gas(), 0x07, 0, input, 0x60, result, 0x40)
            switch success
            case 0 {
                revert(0, 0)
            }
        }
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 6000, "gasleft check error");
    }

    // 0x08
    function callBn256Pairing(bytes memory input)
    public
    returns (bytes32 result)
    {
        // input is a serialized bytes stream of (a1, b1, a2, b2, ..., ak, bk) from (G_1 x G_2)^k
        uint256 len = input.length;
        require(len % 192 == 0);
        uint256 gasLeft1 = gasleft();
        assembly {
            let memPtr := mload(0x40)
            let success := call(
            gas(),
            0x08,
            0,
            add(input, 0x20),
            len,
            memPtr,
            0x20
            )
            switch success
            case 0 {
                revert(0, 0)
            }
            default {
                result := mload(memPtr)
            }
        }
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 100000, "gasleft check error");
    }

    // 0x09
    function callBlake2F(
        uint32 rounds,
        bytes32[2] memory h,
        bytes32[4] memory m,
        bytes8[2] memory t,
        bool f
    ) public returns (bytes32[2] memory) {
        bytes32[2] memory output;

        bytes memory args = abi.encodePacked(
            rounds,
            h[0],
            h[1],
            m[0],
            m[1],
            m[2],
            m[3],
            t[0],
            t[1],
            f
        );
        uint256 gasLeft1 = gasleft();
        assembly {
            if iszero(
            staticcall(not(0), 0x09, add(args, 32), 0xd5, output, 0x40)
            ) {
                revert(0, 0)
            }
        }
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 100000, "gasleft check error");
        return output;
    }

    function addmodTest(
        uint256 x,
        uint256 y,
        uint256 k
    ) public returns (uint256) {
        uint256 gasLeft1 = gasleft();
        uint256 result = addmod(x, y, k);
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        emit callGasUsed(gasUsed);
        require(gasUsed <= 10000 && gasUsed >= 0, "gasleft check error");
        return result;
    }

    function mulmodTest(
        uint256 x,
        uint256 y,
        uint256 k
    ) public view returns (uint256) {
        uint256 gasLeft1 = gasleft();
        uint256 result = mulmod(x, y, k);
        uint256 gasLeft2 = gasleft();
        uint256 gasUsed = gasLeft1 - gasLeft2;
        require(gasUsed <= 10000 && gasUsed >= 0, "gasleft check error");
        return result;
    }

    function gasLimitTest() public view returns (uint256) {
        return block.gaslimit;
    }

    function numberTest() public view returns (uint256) {
        return block.number;
    }

    function timeStampTest() public view returns (uint256) {
        return block.timestamp;
    }

    function callDataTest() public view returns (bytes memory) {
        return msg.data;
    }

    function senderTest() public returns (address) {
        return msg.sender;
    }

    function originTest() public returns (address) {
        return tx.origin;
    }

    function blockHashTest(uint256 blockNumber) public view returns (bytes32) {
        return blockhash(blockNumber);
    }
}
