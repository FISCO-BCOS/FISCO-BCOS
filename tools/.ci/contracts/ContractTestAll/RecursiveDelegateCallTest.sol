// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/RecursiveDelegateCallTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

struct Info {
    address sender;
    address origin;
    address thisAddress;
    uint256 msgValue;
    uint256 thisValue;
    bytes32 codeHash;
    uint256 codeSize;
    bytes32 runtimeCodeHash;
    uint256 runtimeCodeSize;
    uint256 gasPrice;
    uint256 blockNumber;
    uint256 timestamp;
    uint256 gasLimit;
}

contract RecursiveNode {
    event CodeInfo(uint, address);
    event Sender(address);
    event Origin(address);
    event Address(address);
    event MsgValue(uint);
    event ThisValue(uint);
    event CodeHash(bytes32);
    event CodeSize(uint);
    event GasPrice(uint);
    event BlockNumber(uint);
    event Timestamp(uint);
    event GasLimit(uint);

    function getThisCodeSize() public returns(uint){
        address addr = address(this);
        uint len;
        assembly { len := extcodesize(addr) }
        return len;
    }


    function getThisCodeHash() public returns(bytes32){
        address addr = address(this);
        bytes32 codeHash;
        assembly { codeHash := extcodehash(addr) }
        return codeHash;
    }

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

    function getRuntimeCode() public view returns (bytes memory) {
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

    function getRuntimeCodeSize() public view returns(uint256) {
        uint256 size;
        assembly {
            size := codesize()
        }
        return size;
    }

    function recursiveCheck(address[] memory contracts, uint i, Info memory info) public payable {

        if (i >= contracts.length) {
            return;
        }

        emit CodeInfo(i, contracts[i]);

        require(info.sender == msg.sender, "sender not equal");
        require(info.origin == tx.origin, "origin not equal");

        // msg.value: delegatecall 在写法上不允许 addr.delegatecall{value: 1 ether}(""); 这样带上value，但是实际上是透传了value
        require(info.msgValue == msg.value, "msgValue should always be 0 in delegatecall");
        require(info.thisValue == address(this).balance, "thisValue not equal");

        // delegatecall 中不会改变address下的codehash和codesize，只改变了内存中的runtime code
        require(info.codeHash == getThisCodeHash(), "codeHash not equal");
        require(info.codeSize == getThisCodeSize(), "codeSize not equal");
        require(info.runtimeCodeHash == keccak256(getRuntimeCode()), "codeHash not equal with self code");
        require(info.runtimeCodeSize == getRuntimeCode().length, "codeSize not equal");
        require(info.runtimeCodeSize == getRuntimeCodeSize(), "codeSize 2 not equal");
        require(info.codeHash == keccak256(getCode(address(this))), "codeHash not equal with getCode(this)");

        require(info.gasPrice == tx.gasprice, "gasPrice not equal");
        require(info.blockNumber == block.number, "blockNumber not equal");
        require(info.timestamp == block.timestamp, "timestamp not equal");
        require(info.gasLimit == block.gaslimit, "gasLimit not equal");

        (bool ok, bytes memory result) = contracts[i].delegatecall(abi.encodeWithSignature("recursiveCheck(address[],uint256,(address,address,address,uint256,uint256,bytes32,uint256,bytes32,uint256,uint256,uint256,uint256,uint256))",
            contracts, i + 1, info));
        require(ok, string(abi.encodePacked("recursiveCheck must ok, i=", i, " reason=",result)));
    }
}

contract RecursiveDelegateCallTest {
    address[] nodes;
    constructor() public {
        for (uint i = 0; i < 10; i++) {
            nodes.push(address(new RecursiveNode()));
        }
    }

    function getThisCodeSize() public returns(uint){
        address addr = address(this);
        uint len;
        assembly { len := extcodesize(addr) }
        return len;
    }


    function getThisCodeHash() public returns(bytes32){
        address addr = address(this);
        bytes32 codeHash;
        assembly { codeHash := extcodehash(addr) }
        return codeHash;
    }

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

    function check() public payable {

        Info memory info = Info({
            sender: msg.sender,
            origin: tx.origin,
            thisAddress: address(this),
            msgValue: msg.value,
            thisValue: address(this).balance,
            codeHash: getThisCodeHash(),
            codeSize: getThisCodeSize(),
            runtimeCodeHash: RecursiveNode(nodes[0]).getThisCodeHash(),
            runtimeCodeSize: RecursiveNode(nodes[0]).getThisCodeSize(),
            gasPrice: tx.gasprice,
            blockNumber: block.number,
            timestamp: block.timestamp,
            gasLimit: block.gaslimit
        });

        (bool ok, bytes memory result) = nodes[0].delegatecall(abi.encodeWithSignature("recursiveCheck(address[],uint256,(address,address,address,uint256,uint256,bytes32,uint256,bytes32,uint256,uint256,uint256,uint256,uint256))",
            nodes, 0, info));
        require(ok, string(abi.encodePacked("recursiveCheck must ok. reason=",result)));
    }
}