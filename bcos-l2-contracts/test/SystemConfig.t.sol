// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import "forge-std/Test.sol";
import {SystemConfig} from "../src/SystemConfig.sol";
import {ISystemConfig} from "../src/interfaces/ISystemConfig.sol";

contract SystemConfigTest is Test {
    SystemConfig cfg;
    address owner = address(0xCAFE);

    function setUp() public {
        cfg = new SystemConfig();
        cfg.initialize(owner);
    }

    function test_SetGetValueByKey_RoundTrip() public {
        vm.prank(owner);
        cfg.setValueByKey("tx_gas_limit", 3_000_000_000, 5);
        (uint192 v, uint64 e) = cfg.getValueByKey("tx_gas_limit");
        assertEq(uint256(v), 3_000_000_000);
        assertEq(uint256(e), 5);
    }

    function test_GetUnsetKey_ReturnsZero() public view {
        (uint192 v, uint64 e) = cfg.getValueByKey("never_set");
        assertEq(uint256(v), 0);
        assertEq(uint256(e), 0);
    }

    function test_SetValueByKey_OnlyOwner() public {
        vm.prank(address(0xBEEF));
        vm.expectRevert("Ownable: caller is not the owner");
        cfg.setValueByKey("tx_gas_limit", 1, 0);
    }

    function test_SetValueByKey_EmitsConfigUpdate() public {
        vm.expectEmit(true, false, false, true);
        emit ISystemConfig.ConfigUpdate("web3_chain_id", 901, 5);
        vm.prank(owner);
        cfg.setValueByKey("web3_chain_id", 901, 5);
    }

    /// value (192 bit) and enableNumber (64 bit) share one slot; setting one to
    /// its max must not bleed into the other.
    function test_Packing_ValueAndEnableIndependent() public {
        uint192 bigV = type(uint192).max;
        uint64 bigE = type(uint64).max;
        vm.startPrank(owner);
        cfg.setValueByKey("a", bigV, 0);
        cfg.setValueByKey("b", 0, bigE);
        vm.stopPrank();
        (uint192 va, uint64 ea) = cfg.getValueByKey("a");
        (uint192 vb, uint64 eb) = cfg.getValueByKey("b");
        assertEq(uint256(va), uint256(bigV));
        assertEq(uint256(ea), 0);
        assertEq(uint256(vb), 0);
        assertEq(uint256(eb), uint256(bigE));
    }

    function test_Overwrite_LastWriteWins() public {
        vm.startPrank(owner);
        cfg.setValueByKey("k", 1, 10);
        cfg.setValueByKey("k", 2, 20);
        vm.stopPrank();
        (uint192 v, uint64 e) = cfg.getValueByKey("k");
        assertEq(uint256(v), 2);
        assertEq(uint256(e), 20);
    }

    function test_Initialize_CannotBeCalledTwice() public {
        vm.expectRevert("Initializable: contract is already initialized");
        cfg.initialize(address(0xD00D));
    }

    /// @dev `_config` lands at slot 101 after the OZ v4.7 upgradeable base storage
    ///      (Initializable[0] + ContextUpgradeable __gap[1-50] + _owner[51] +
    ///      __gap[52-100]). Mirror of storage-layout/SystemConfig.json; the C++
    ///      reader hardcodes this baseSlot.
    uint256 internal constant CONFIG_BASE_SLOT = 101;

    /// Cross-language storage contract: the L2 upper layers read a key's slot
    /// directly (no EVM) via keccak256(utf8(key) ‖ be32(baseSlot)), then split the
    /// word into value(low 192) / enableNumber(high 64). This test asserts the
    /// Solidity mapping addressing equals that exact formula — if it ever diverges
    /// (e.g. someone adds state before _config, shifting baseSlot), the C++ reader
    /// would read garbage, and this fails first.
    function test_SlotAddressing_MatchesKeccakFormula() public {
        vm.prank(owner);
        cfg.setValueByKey("web3_chain_id", 901, 5);
        bytes32 slot = keccak256(abi.encodePacked("web3_chain_id", CONFIG_BASE_SLOT));
        uint256 word = uint256(vm.load(address(cfg), slot));
        assertEq(word & ((uint256(1) << 192) - 1), 901, "value = low 192 bits");
        assertEq(word >> 192, 5, "enableNumber = high 64 bits");
    }
}
