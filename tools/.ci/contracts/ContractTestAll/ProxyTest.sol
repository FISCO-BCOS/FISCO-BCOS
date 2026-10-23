// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/ProxyTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh

import "./DelegateCall.sol";

/**
 * @title Proxy
 * @dev Gives the possibility to delegate any call to a foreign implementation.
 */
abstract contract Proxy {
    /**
     * @dev Tells the address of the implementation where every call will be delegated.
     * @return address of the implementation to which it will be delegated
     */
    function implementation() virtual public view returns (address);

    /**
     * @dev Tells the type of proxy (EIP 897)
     * @return proxyTypeId Type of proxy, 2 for upgradeable proxy
     */
    function proxyType() virtual public pure returns (uint256 proxyTypeId);

    /**
     * @dev Fallback function allowing to perform a delegatecall to the given implementation.
     * This function will return whatever the implementation call returns
     */
    fallback () external payable {
        address _impl = implementation();
        require(_impl != address(0), "Proxy implementation required");

        assembly {
            let ptr := mload(0x40)
            calldatacopy(ptr, 0, calldatasize())
            let result := delegatecall(gas(), _impl, ptr, calldatasize(), 0, 0)
            let size := returndatasize()
            returndatacopy(ptr, 0, size)

            switch result
            case 0 { revert(ptr, size) }
            default { return(ptr, size) }
        }
    }
}

contract ProxyImpl is Proxy {
    // put implementation address as far as possible to make sure that it will not be at the same location as in the Proxy contract
    address[1024] private gaps_;
    address public implementation_;

    constructor(address implementation) public {
        implementation_ = implementation;
    }

    function implementation() override public view returns (address) {
        return implementation_;
    }

    function proxyType() override public pure returns (uint256 proxyTypeId) {
        return 2;
    }
}

contract ProxyTest {
    event Info(string, address);
    event RunInfo(string, string);
    Proxy proxy = new ProxyImpl(address(new DelegateCall()));


    function run(string memory func) private {
        (bool success, bytes memory ret) = address(proxy).call(abi.encodeWithSignature(func));
        if (success) {
            emit RunInfo(func, "success");
        } else {
            emit RunInfo(func, "failed");
        }
    }

    function check() public {
        run("testFailed()");
        run("testSuccess()");
        run("testFallback()");
        run("testCallInDelegateCall()");
        run("testDelegateCallSender()");
        run("testCallcodeSender()");
        run("testEventInDelegateCall()");
        run("testCatchInDelegateCallThrow()");
    }

    function test() public {
        address impl = proxy.implementation();
        emit Info("DelegateCall contract addr", impl);
        DelegateCall(impl).testEventInDelegateCall();
    }

    function test1() public {
        run("testEventInDelegateCall()");
    }
}
