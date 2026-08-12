// SPDX-License-Identifier: Apache-2.0
pragma solidity 0.8.25;

import "forge-std/Test.sol";
import {SystemConfig} from "../src/SystemConfig.sol";
import {ISystemConfig} from "../src/interfaces/ISystemConfig.sol";

contract SystemConfigTest is Test {
    SystemConfig cfg;
    address owner = address(0xCAFE);

    // The only runtime-writable key (D4 authority boundary).
    string constant WRITABLE_KEY = "block_tx_count_limit";

    function setUp() public {
        cfg = new SystemConfig();
        cfg.initialize(owner);
    }

    function test_SetGetValueByKey_RoundTrip() public {
        vm.prank(owner);
        cfg.setValueByKey(WRITABLE_KEY, 3_000, 5);
        (uint192 v, uint64 e) = cfg.getValueByKey(WRITABLE_KEY);
        assertEq(uint256(v), 3_000);
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
        cfg.setValueByKey(WRITABLE_KEY, 1, 0);
    }

    /// D4 authority boundary: frozen keys revert even for the owner —
    /// chain_id (chain identity) and gas_limit (authority = op-node payload
    /// attributes) permanently; compatibility_version / feature_flags
    /// (state-transition semantics; a runtime write would fork from the OP
    /// oracle) until a governed, oracle-synced change path exists.
    function test_SetValueByKey_FrozenKeysRevert() public {
        string[4] memory frozen = ["chain_id", "gas_limit", "compatibility_version", "feature_flags"];
        vm.startPrank(owner);
        for (uint256 i = 0; i < frozen.length; i++) {
            vm.expectRevert("SystemConfig: key not runtime-writable");
            cfg.setValueByKey(frozen[i], 1, 0);
        }
        vm.stopPrank();
    }

    /// Whitelist semantics: an unknown key is rejected by default; widening
    /// the writable set requires a contract upgrade.
    function test_SetValueByKey_UnknownKeyReverts() public {
        vm.prank(owner);
        vm.expectRevert("SystemConfig: key not runtime-writable");
        cfg.setValueByKey("some_future_key", 1, 0);
    }

    function test_SetValueByKey_EmitsConfigUpdate() public {
        vm.expectEmit(true, false, false, true);
        emit ISystemConfig.ConfigUpdate(WRITABLE_KEY, 901, 5);
        vm.prank(owner);
        cfg.setValueByKey(WRITABLE_KEY, 901, 5);
    }

    /// value (192 bit) and enableNumber (64 bit) share one slot; setting one to
    /// its max must not bleed into the other.
    function test_Packing_ValueAndEnableIndependent() public {
        uint192 bigV = type(uint192).max;
        uint64 bigE = type(uint64).max;
        vm.startPrank(owner);
        cfg.setValueByKey(WRITABLE_KEY, bigV, 0);
        (uint192 v1, uint64 e1) = cfg.getValueByKey(WRITABLE_KEY);
        assertEq(uint256(v1), uint256(bigV));
        assertEq(uint256(e1), 0);
        cfg.setValueByKey(WRITABLE_KEY, 0, bigE);
        (uint192 v2, uint64 e2) = cfg.getValueByKey(WRITABLE_KEY);
        assertEq(uint256(v2), 0);
        assertEq(uint256(e2), uint256(bigE));
        vm.stopPrank();
    }

    function test_Overwrite_LastWriteWins() public {
        vm.startPrank(owner);
        cfg.setValueByKey(WRITABLE_KEY, 1, 10);
        cfg.setValueByKey(WRITABLE_KEY, 2, 20);
        vm.stopPrank();
        (uint192 v, uint64 e) = cfg.getValueByKey(WRITABLE_KEY);
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
        cfg.setValueByKey(WRITABLE_KEY, 901, 5);
        bytes32 slot = keccak256(abi.encodePacked(WRITABLE_KEY, CONFIG_BASE_SLOT));
        uint256 word = uint256(vm.load(address(cfg), slot));
        assertEq(word & ((uint256(1) << 192) - 1), 901, "value = low 192 bits");
        assertEq(word >> 192, 5, "enableNumber = high 64 bits");
    }
}
