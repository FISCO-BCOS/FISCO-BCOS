// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/BalanceReceiveTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.0 <0.8.12;
pragma experimental ABIEncoderV2;

contract ReceiveAndCallbackFunction {
    event Info(string, uint256);
    // receive function
    receive() external payable {
        require(msg.value > 10, "receive() msg.value is zero");
        emit Info("receive() msg.value", msg.value);
    }

    // fallback function
    fallback() external payable {
        require(msg.value > 0, "fallback() msg.value is zero");
        emit Info("fallback() msg.value", msg.value);
    }
}

contract OnlyReceiveFunction {
    event Info(string, uint256);
    // receive function
    receive() external payable {
        require(msg.value > 10, "receive() msg.value is zero");
        emit Info("receive() msg.value", msg.value);
    }
}

contract OnlyFallbackFunction {

    event Info(string, uint256);

    // fallback function
    fallback() external payable {
        require(msg.value > 0, "fallback() msg.value is zero");
        emit Info("fallback() msg.value", msg.value);
    }
}


contract BalanceReceiveTest {
    modifier mustHasBalance() {
        require(address(this).balance > 0, "balance not enough");
        _;
    }

    function callTransfer(address payable to, uint256 amount) public payable {
        to.transfer(amount);
    }

    function callFallback(address payable to, uint256 amount) public payable {
        (bool success, bytes memory reason) = to.call{value: amount}("aaa()");
        require(success, string(abi.encodePacked("callFallback failed: ", reason)));
    }

    function checkOnlyFallbackFunction() public payable {
        uint256 callValue = 7;

        OnlyFallbackFunction contractAddress = new OnlyFallbackFunction();
        // must success
        try this.callTransfer(payable(contractAddress), callValue) {
        } catch (bytes memory reason) {
            require(false, string(abi.encodePacked("checkOnlyFallbackFunction callTransfer with value failed: ", reason)));
        }

        require(address(contractAddress).balance == callValue, "checkReceive failed: balance not equal");

        // must failed transfer 0
        try this.callTransfer(payable(contractAddress), 0) {
            require(false, "checkOnlyFallbackFunction callTransfer without value should revert");
        } catch {
        }

        // must success on fallback value is not 0
        try this.callFallback(payable(contractAddress), callValue) {
        } catch (bytes memory reason) {
            require(false, string(abi.encodePacked("checkOnlyFallbackFunction callFallback with value failed: ", reason)));
        }

        require(address(contractAddress).balance == callValue * 2, "checkReceive failed: balance not equal");

        // must failed on fallback  value is 0
        try this.callFallback(payable(contractAddress), 0) {
            require(false, "checkOnlyFallbackFunction callFallback without value should revert");
        } catch {
        }
    }

    function checkOnlyReceiveFunction() public payable {
        uint256 callValue = 17;

        OnlyReceiveFunction contractAddress = new OnlyReceiveFunction();
        // must success
        try this.callTransfer(payable(contractAddress), callValue) {
        } catch (bytes memory reason) {
            require(false, string(abi.encodePacked("checkOnlyReceiveFunction callTransfer with value failed: ", reason)));
        }

        require(address(contractAddress).balance == callValue, "checkReceive failed: balance not equal");

        // must failed transfer lesser than 10
        try this.callTransfer(payable(contractAddress), 1) {
            require(false, "checkOnlyReceiveFunction callTransfer without value should revert");
        } catch {
        }

        // must failed on fallback value is over 10
        try this.callFallback(payable(contractAddress), callValue) {
            require(false, "checkOnlyReceiveFunction callFallback with value should revert");
        } catch {
        }

        require(address(contractAddress).balance == callValue, "checkReceive failed: balance not equal");

        // must failed on fallback  value is lesser than 10
        try this.callFallback(payable(contractAddress), 1) {
            require(false, "checkOnlyReceiveFunction callFallback without value should revert");
        } catch {
        }
    }

    function checkReceiveAndFallbackFunction() public payable {
        uint256 callValue = 17;
        ReceiveAndCallbackFunction contractAddress = new ReceiveAndCallbackFunction();
        // must success
        try this.callTransfer(payable(contractAddress), callValue) {
        } catch (bytes memory reason) {
            require(false, string(abi.encodePacked("checkReceiveAndFallbackFunction callTransfer with value failed: ", reason)));
        }

        require(address(contractAddress).balance == callValue, "checkReceive failed: balance not equal");

        // must failed transfer 0
        try this.callTransfer(payable(contractAddress), 1) {
            require(false, "checkReceiveAndFallbackFunction callTransfer without value should revert");
        } catch {
        }
        require(address(contractAddress).balance == callValue, "checkReceive failed: balance not equal");

        uint256 callValue2 = 5;

        // must success on fallback value is not 0
        try this.callFallback(payable(contractAddress), callValue2) {
        } catch (bytes memory reason) {
            require(false, string(abi.encodePacked("checkReceiveAndFallbackFunction callFallback with value failed: ", reason)));
        }

        require(address(contractAddress).balance == callValue + callValue2, "checkReceive failed: balance not equal");

        // must failed on fallback  value is 0
        try this.callFallback(payable(contractAddress), 0) {
            require(false, "checkReceiveAndFallbackFunction callFallback without value should revert");
        } catch {
        }

        require(address(contractAddress).balance == callValue + callValue2, "checkReceive failed2: balance not equal");
    }

    function check() public payable mustHasBalance {
        checkOnlyFallbackFunction();
        checkOnlyReceiveFunction();
        checkReceiveAndFallbackFunction();
    }
}