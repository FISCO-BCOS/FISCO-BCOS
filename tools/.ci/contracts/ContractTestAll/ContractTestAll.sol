// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/ContractTestAll.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
import "./CodeOp.sol";
import "./Create2.sol";
import "./DelegateCall.sol";
import "./RecursiveDelegateCallTest.sol";
import "./EventTest.sol";
import "./LibraryTest.sol";
import "./ProxyTest.sol";
import "./StaticCall.sol";
import "./TablePrecompiledTest.sol";
import "./ECRecoverTest.sol";
import "./DeployTreeTest.sol"; // DMC
import "./BalanceTest.sol";
import "./BalancePrecompiledTest.sol";
import "./BalanceReceiveTest.sol";

contract ContractTestAll {
    event Info(string, uint256, uint256);

    function checkOne(string memory name, address addr, bool needProxyCheck, uint256 callValue) private {
        uint256 gasBefore = gasleft();
        (bool success, bytes memory reason) = address(addr).call{value: callValue}(abi.encodeWithSignature("check()"));
        require(success, string(abi.encodePacked(name, " check failed: ", reason)));

        uint256 gasBeforeProxy = gasleft();

        if (needProxyCheck) {
            // use proxy to call check()
            Proxy proxy = new ProxyImpl(addr);
            (success, reason) = address(proxy).call{value: callValue}(abi.encodeWithSignature("check()"));
            require(success, string(abi.encodePacked(name, " proxy check failed: ", reason)));
        }
        uint256 gasAfter = gasleft();
        emit Info(string(abi.encodePacked(name, " gas used <non-proxy, proxy>")), gasBefore - gasBeforeProxy, gasBeforeProxy - gasAfter);
    }

    function check() public payable {
        uint256 thisBalance = address(this).balance;
        uint256 callValue = thisBalance > 100000 ? 5000 : 0;

        checkOne("CodeOp", address(new CodeOp()), false, 0);
        checkOne("Create2", address(new Create2()), true, 0);
        checkOne("DelegateCall", address(new DelegateCall()), true, 0);
        checkOne("RecursiveDelegateCallTest", address(new RecursiveDelegateCallTest()), false, 0);
        checkOne("EventTest", address(new EventTest()), true, 0);
        checkOne("LibraryTest", address(new LibraryTest()), true, 0);
        checkOne("ProxyTest", address(new ProxyTest()), true, 0);
        checkOne("TablePrecompiledTest", address(new TablePrecompiledTest()), true, 0);
        checkOne("ECRecoverTest", address(new ECRecoverTest()), true, 0);
        checkOne("DeployTreeTest", address(new DeployTreeTest()), true, 0); // DMC
        checkOne("BalanceTest", address(new BalanceTest()), true, callValue);
        checkOne("BalancePrecompiledTest", address(new BalancePrecompiledTest()), false, 0);
        checkOne("BalanceReceiveTest", address(new BalanceReceiveTest()), true, callValue);

        checkOne("StaticCall", address(new StaticCall()), false, 0); // must at last for gas will be used up by staticcall failed
    }

    // fallback
    fallback() external payable {}
}
