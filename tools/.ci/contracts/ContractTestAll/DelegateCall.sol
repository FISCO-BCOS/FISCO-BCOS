// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/DelegateCall.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.0 <0.8.12;

contract DelegateCallDest {
    int public value = 0;
    address public sender;
    address public myAddress;

    event Info(string, address, address);

    constructor() public {
        myAddress = address(this);
    }

    function add() public returns(bytes memory) {
        sender = msg.sender;
        value += 2;
        return "2";
    }

    function callMyAddress() public {
        bool success;
        bytes memory ret;
        (success, ret) = myAddress.call(abi.encodeWithSignature("recordSender()"));
    }

    function mustRevert() public {
        require(false, "DelegateCallDest revert");
    }

    function callMyAddressRevert() public returns(bytes memory) {
        try DelegateCall(myAddress).mustRevert() {
            return "no catch";
        } catch (bytes memory reason) {
            return reason;
        }
    }


    function emitInfo() public {
        emit Info("Dest, this, msg.sender", address(this), msg.sender);
    }

    fallback() external {
        value += 1000;
    }
}

contract DelegateAtConstruct {
    constructor(address addr) public {
        addr.delegatecall(abi.encodeWithSignature("add()"));
    }
    /*
        function value() public returns(bytes memory) {
            (bool ok, bytes memory result) = addr.delegatecall(abi.encodeWithSignature("value()"));
            return result;
        }
        */
}

contract EmptyContract {}

contract DelegateCall {
    int public value = 0;
    address public sender;
    address public myAddress;
    uint myCodeSize;
    bytes32 myCodeHash;
    address public delegateDest;
    address public latestSender;
    address public latestOrigin;

    event Info(string, address, address);
    event InfoBytes(string, bytes, bytes32);

    modifier initIfNotInit() {
        if (myAddress == address(0)) {
            myAddress = address(this);
        }
        if (delegateDest == address(0)) {
            delegateDest = address(new DelegateCallDest());
        }
        _;
    }

    constructor() public initIfNotInit {
    }

    function add() public initIfNotInit returns(bytes memory) {
        sender = msg.sender;
        value += 1;
        return "1";
    }

    function recordSender() public initIfNotInit{
        latestSender = msg.sender;
        latestOrigin = tx.origin;
    }

    function testCallNoAddress() public initIfNotInit{
        int v = value;
        address addr = address(0x1001);
        (bool ok, bytes memory result) = dCall(addr, "add()");
        require(ok, "addr not exist but must return ok to be the same as eth");
        require(v == value, "testCallNoAddress value must no change");
    }

    function testCallNoSelector() public initIfNotInit{
        // has addr but selector is not exist
        address emptyContract = address(new EmptyContract());
        (bool ok, bytes memory result) = dCall(emptyContract, "notExistsFunction()");
        require(!ok, "testSuccess must not ok");
    }

    function testSuccess() public initIfNotInit {
        int v = value;
        (bool ok, bytes memory result) = dCall(delegateDest, "add()");
        require(ok, "testSuccess must ok");
        require(v + 2 == value, "testSuccess value must +2");
    }

    function testFallback() public initIfNotInit returns(bytes memory){
        int v = value;
        (bool ok, bytes memory result) = dCall(delegateDest, "triggerFallback()");
        require(v + 1000 == value, "testFallback value must +1000");
    }

    function address2HexString(address x) internal pure returns (string memory) {
        bytes memory s = new bytes(40);
        for (uint i = 0; i < 20; i++) {
            bytes1 b = bytes1(uint8(uint(uint160(x)) / (2**(8*(19 - i)))));
            bytes1 hi = bytes1(uint8(b) / 16);
            bytes1 lo = bytes1(uint8(b) - 16 * uint8(hi));
            s[2*i] = char(hi);
            s[2*i+1] = char(lo);
        }
        return string(s);
    }

    function char(bytes1 b) internal pure returns (bytes1 c) {
        if (uint8(b) < 10) return bytes1(uint8(b) + 0x30);
        else return bytes1(uint8(b) + 0x57);
    }

    function dumpInfo(string memory msg, address addr1, address addr2) public pure returns(string memory) {
        return string(abi.encodePacked(msg, address2HexString(addr1), " | ", address2HexString(addr2)));
    }

    function testCallInDelegateCall() public initIfNotInit {
        dCall(delegateDest, "callMyAddress()");
        require(address(this) == latestSender, dumpInfo("callInDelegateCall's msg.sender should be this contract ", address(this), latestSender));
        require(tx.origin == latestOrigin, dumpInfo("callInDelegateCall's tx.origin should be this contract ", address(tx.origin), latestOrigin));
    }

    function emitInfo() public initIfNotInit {
        emit Info("Base, this, msg.sender", address(this), msg.sender);
    }

    function testEventInDelegateCall() public initIfNotInit {
        emitInfo();
        dCall(delegateDest, "emitInfo()");
        //delegateDest.call(abi.encodeWithSignature("emitInfo()"));
        emitInfo();
    }

    function testDelegateCallSender() public initIfNotInit {
        dCall(delegateDest, "add()");
        require(msg.sender == sender, "delegatecall's sender must be msg.sender");
    }

    function testCallcodeSender() public initIfNotInit {
        address addr = delegateDest;
        bytes memory payload = abi.encodeWithSignature("add()");
        assembly {
            let success := callcode(gas(), addr, 0, add(payload, 0x20), mload(payload), 0, 0)
        }

        require(address(this) == sender, "callcode's sender must be this address");
    }

    function testDelegateCallAtConstruct() public initIfNotInit returns(address) {
        DelegateAtConstruct test = new DelegateAtConstruct(delegateDest);
        return address(test);
    }

    function mustRevert() public initIfNotInit {
        require(false, "Base revert");
    }

    function checkBytes(bytes memory origin, bytes32 hash, bytes32 smHash) public pure returns(bool) {
        return keccak256(origin) == hash || keccak256(origin) == smHash;
    }

    function testCatchInDelegateCallThrow() public initIfNotInit {
        (bool ok, bytes memory result) = dCall(delegateDest, "callMyAddressRevert()");
        require(ok, "testCatchInDelegateCallThrow must not ok");
        emit InfoBytes("delegatecall result", result, keccak256(result));

        // check the same as remix
        require(checkBytes(result, bytes32(0x4dd31d082b4aaf0e899854bf1871e7f70e3794b3ec764ad291ab64cbdff7910d),
            bytes32(0xc770ee3912bb8b54fdeb59371079323aac0cca9ca0355dfddc07d5ee38c8f35b)), "testCatchInDelegateCallThrow checkBytes failed");

        try this.mustRevert() {
            emit Info("no catch", address(this), msg.sender);
            require(false, "must catch revert");
        } catch (bytes memory reason) {
            emit InfoBytes("this call result", reason, keccak256(reason));
            // check the same as remix
            require(checkBytes(reason, bytes32(0x0e58f8cab8bcea7dbf8463f931105075f2375b58db9353a6a1bc982fa9e84acf),
                bytes32(0x340af06566b98237072abe7f5c4c5a7c39059c64f09ea1de31622cb74f3e3be5)), "testCatchInDelegateCallThrow this call result checkBytes failed");
        }
    }

    function dCall(address addr, string memory func) private returns(bool, bytes memory) {
        return addr.delegatecall(abi.encodeWithSignature(func));
    }

    function codesizeAt(address addr) public initIfNotInit returns(uint){
        uint len;
        assembly { len := extcodesize(addr) }
        return len;
    }

    function codehashAt(address addr) public view returns(bytes32){
        bytes32 codeHash;
        assembly { codeHash := extcodehash(addr) }
        return codeHash;
    }


    function check() public initIfNotInit {
        testCallNoAddress();
        testCallNoSelector();
        testSuccess();
        testFallback();
        testCallInDelegateCall();
        testDelegateCallSender();
        testCallcodeSender();
        testEventInDelegateCall();
        testCatchInDelegateCallThrow();
    }

}
