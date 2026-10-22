// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/BalanceTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.0 <0.8.12;

contract BalanceTest {
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Balance(uint256 value);
    event Info(string info);
    event Info(uint id, string info);
    event Info(address addr, string info);

    modifier mustHasBalance() {
        require(getSelfBalance() > 0, "balance not enough");
        _;
    }

    function getBalance(address addr) public view returns(uint256) {
        return addr.balance;
    }

    function getSelfBalance() public view returns(uint256) {
        return address(this).balance;
    }

    function transfer(address payable to, uint256 amount) public payable {
        uint256 balanceBefore = getBalance(to);
        uint256 balanceBeforeSelf = getSelfBalance();
        to.transfer(amount);
        uint256 balanceAfter = getBalance(to);
        uint256 balanceAfterSelf = getSelfBalance();
        //require(balanceAfter - balanceBefore == amount, "balance should be increased by amount");
        //require(balanceBeforeSelf - balanceAfterSelf == amount, "self balance should be decreased by amount");
        emit Transfer(address(this), to, amount);
    }

    function testSelfBalance() public {
        require(getSelfBalance() != 0, "self balance should not be 0");
        require(getBalance(address(this)) == getSelfBalance(), "self balance should be 0");
    }

    function receiveBalance() public payable {
        require(msg.value > 0, "msg.value should be greater than 0");
        emit Balance(msg.value);
    }

    function testReceiveBalance() public payable {
        /*
        // must revert
        try this.receiveBalance() {
            revert("should revert");
        } catch (bytes memory reason) {
        }
        */

        this.receiveBalance{value: 1}();
    }

    function notPayable() public {
        emit Info("Into notPayable");
    }

    function testCallNotPayableWithValue() public payable {
        // use call function to call notPayable
        (bool success,  bytes memory ret) = address(this).call{value: 1}(abi.encodeWithSignature("notPayable()"));
        require(!success, "call notPayable with value must failed");
    }

    function testDeployWithValue() public payable returns(address){
        // must success
        address successAddr = address(new ContractWithPayableConstructor{value: 1}());
        require(successAddr != address(0x0), "deploy failed");

        return successAddr;
    }

    function testTransferBalanceToPrecompiled() public mustHasBalance {
        // should be the same as eth
        address [22] memory okAddresses = [address((0x0)), address(0x2), address(0x3), address(0x4), address(0x5),
                        address(0x6), address(0xa), address(0xb), address(0xc), address(0xd), address(0xe), address(0xf),
                        address(0x10), address(0x11), address(0x12), address(0x13), address(0x14), address(0x15),
                        address(0x16), address(0x17), address(0x18), address(0x19)];

        address [4] memory errorAddresses = [address(0x1), address(0x7), address(0x8), address(0x9)];

        // foreach okAddresses
        for (uint256 i = 0; i < okAddresses.length; i++) {
            address payable addr = payable (okAddresses[i]);
            try this.transfer(addr, 1) {
                emit Info(addr, "success");
            } catch (bytes memory reason) {
                emit Info(addr, "false");
                revert(string(abi.encodePacked("should success, but failed, reason: ", reason)));
            }
        }

        // foreach errorAddresses
        for (uint256 i = 0; i < errorAddresses.length; i++) {
            address payable addr = payable (errorAddresses[i]);
            try this.transfer(addr, 1) {
                emit Info(addr, "success");
                revert("should revert");
            } catch (bytes memory reason) {
                emit Info(addr, "false");
            }
        }
    }

    function testTransferBalance() public mustHasBalance {
        uint256 balanceBefore = getBalance(msg.sender);
        uint256 balanceBeforeSelf = getSelfBalance();
        address payable sender = payable(msg.sender);
        sender.transfer(1);
        uint256 balanceAfter = getBalance(msg.sender);
        uint256 balanceAfterSelf = getSelfBalance();
        require(balanceAfter - balanceBefore == 1, "balance should be increased by amount");
        require(balanceBeforeSelf - balanceAfterSelf == 1, "self balance should be decreased by amount");
    }

    event Info2(string info, address addr, uint256 value);
    function testTransferBalanceToOrigin() public mustHasBalance {
        uint256 balanceBefore = getBalance(tx.origin);
        uint256 balanceBeforeSelf = getSelfBalance();
        address payable origin = payable(tx.origin);
        origin.transfer(1);
        uint256 balanceAfter = getBalance(tx.origin);
        uint256 balanceAfterSelf = getSelfBalance();
        require(balanceAfter - balanceBefore == 1, "balance should be increased by amount");
        require(balanceBeforeSelf - balanceAfterSelf == 1, "self balance should be decreased by amount");
        emit Info2("testTransferBalanceToOrigin", tx.origin, balanceAfter);
    }

    function testSelfdestruct() public payable {
        uint256 balanceBefore = getBalance(msg.sender);
        uint256 balanceBeforeSelf = getSelfBalance();
        selfdestruct(payable(msg.sender));
        uint256 balanceAfter = getBalance(msg.sender);
        uint256 balanceAfterSelf = getSelfBalance();
        require(balanceAfter - balanceBefore == balanceBeforeSelf, "balance should be increased by amount");
        require(balanceAfterSelf == 0, "self balance should be 0");
    }

    function testSelfdestructZeroAddress() public payable {
        //uint256 balanceBefore = getBalance(msg.sender);
        //uint256 balanceBeforeSelf = getSelfBalance();
        selfdestruct(payable(0x0));
        //uint256 balanceAfter = getBalance(msg.sender);
        //uint256 balanceAfterSelf = getSelfBalance();
        //require(balanceAfter - balanceBefore == balanceBeforeSelf, "balance should be increased by amount");
        //require(balanceAfterSelf == 0, "self balance should be 0");
    }

    function pureTransfer(address payable to, uint256 amount) public payable {
        to.transfer(amount);
    }

    function testBaseFee() public view returns (uint256) {
        require(block.difficulty == 0, "basefee must be 0");
    }

    function getGasPrice() public view returns (uint256) {
        return tx.gasprice;
    }

    function testGasPrice() public payable{
        AnotherContract b = new AnotherContract();
        require(getGasPrice() == b.getGasPrice(), "gas price should be the same");
    }

    function testMsgValue() public payable mustHasBalance {
        this.testMsgValueInternal{value: 1}();
    }

    function testMsgValueInternal() public payable {
        AnotherContract b = new AnotherContract();
        require(msg.value == b.getMsgValue{value: msg.value}(), "msg value should be the same");
    }

    // must call by admin
    function check() public mustHasBalance payable {
        testSelfBalance();
        testReceiveBalance();
        testCallNotPayableWithValue();
        testDeployWithValue();
        testTransferBalance();
        testTransferBalanceToOrigin();
        testBaseFee();
        testGasPrice();
        testMsgValue();
        testTransferBalanceToPrecompiled();
        testSelfdestruct();
        //testSelfdestructZeroAddress();

        /* testTransferIndelegateCall check sender
          testTransferToPrecompiledIndelegateCall
testTransferBalance();
testTransferBalanceByCall();
testTransferBalanceByDelegateCall();
testTransferBalanceByCallCode();
testTransferBalanceByStaticCall();

testTransferBalanceBack();

testSelfdestructToSelf();
testSelfdestructToPrecompiled();
testTransferBalanceAfterSelfdestruct();
testSelfdestructAfterTransferBalance();
testSelfdestructAfterSelfdestruct();
*/
    }
}

contract AnotherContract {
    function getGasPrice() public view returns (uint256) {
        return tx.gasprice;
    }
    function getMsgValue() public payable returns (uint256) {
        return msg.value;
    }
}

contract ContractWithPayableConstructor {
    event Balance(uint256);
    string name;

    constructor() public payable {
        emit Balance(address(this).balance);
        name = "Hello, World!";
    }

    function get() public view returns (string memory) {
        return name;
    }

    function set(string memory n) public {
        name = n;
    }
}

contract ContractWithNonPayableConstructor {
    event Balance(uint256);
    string name;

    constructor() public {
        emit Balance(address(this).balance);
        name = "Hello, World!";
    }

    function get() public view returns (string memory) {
        return name;
    }

    function set(string memory n) public {
        name = n;
    }
}

