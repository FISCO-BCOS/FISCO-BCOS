// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/CodeOp.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.0 <0.8.12;

contract CodeOp {
    // CODESIZE

    // CODECOPY

    // EXTCODESIZE

    // EXTCODECOPY

    // EXTCODEHASH

    function getCode(address _contractAddress) public view returns (bytes memory) {
        uint256 codeSize;
        assembly {
            codeSize := extcodesize(_contractAddress)
        }
        require(codeSize > 0, "Contract does not exist");

        bytes memory code = new bytes(codeSize);
        assembly {
            extcodecopy(_contractAddress, add(code, 32), 0, codeSize)
        }
        return code;
    }

    function getSelfCode() public view returns (bytes memory) {
        uint256 codeSize;
        assembly {
            codeSize := codesize()
        }
        require(codeSize > 0, "Contract does not exist");

        bytes memory code = new bytes(codeSize);
        assembly {
            codecopy(add(code, 32), 0, codeSize)
            mstore(code, codeSize)
        }
        return code;
    }

    function codeHashAt(address addr) public view returns(bytes32){
        bytes32 codeHash;
        assembly {
            codeHash := extcodehash(addr)
        }
        return codeHash;
    }

    function codeSize() public view returns(uint256) {
        uint256 size;
        assembly {
            size := codesize()
        }
        return size;
    }

    function extcodeSize(address a) public view returns(uint256) {
        uint256 size;
        assembly {
            size := extcodesize(a)
        }
        return size;
    }

    function checkEthPrecompiledCode() public view returns(uint256){
        require(extcodeSize(0x0000000000000000000000000000000000000001) == 0, "eth precompiled code size must be 0");
        require(codeHashAt(0x0000000000000000000000000000000000000001) == bytes32(0), "eth precompiled code hash must be 0");
    }

    function checkSelfCode() public returns(bytes32, bytes32) {
        bytes32 codeHash = codeHashAt(address(this));
        bytes memory extcode = getCode(address(this));
        bytes memory selfCode = getSelfCode();

        require(keccak256(extcode) == codeHash, "extcode must same");
        require(keccak256(selfCode) == codeHash, "selfCode must same");
        return (codeHash, keccak256(extcode));
    }

    function checkCodeSize() public {
        bytes memory code = getSelfCode();
        require(code.length == codeSize(), "codeSize must same");
        require(code.length == extcodeSize(address(this)), "extcodeSize must same");
    }

    function check() public {
        checkSelfCode();
        checkCodeSize();
        checkEthPrecompiledCode();
    }
}