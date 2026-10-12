"""build-allocs SystemConfig feature_flags guard: keyed on the role NAME, not an address.

The guard refuses a SystemConfig predeploy whose system_config lacks feature_flags —
on every layout (the template's 0x43...C0 and the committed C2 layout's 0x4200...1000
alike), because the check reads the predeploy's name, not its address.
"""

import importlib.util
import pathlib

import pytest

spec = importlib.util.spec_from_file_location(
    "build_allocs", pathlib.Path(__file__).with_name("build-allocs.py"))
build_allocs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_allocs)


def test_system_config_role_without_flags_is_refused():
    with pytest.raises(ValueError, match="feature_flags"):
        build_allocs.require_system_config_flags({"name": "SystemConfig"})


def test_system_config_role_with_flags_passes():
    build_allocs.require_system_config_flags(
        {"name": "SystemConfig", "system_config": {"feature_flags": "0x0"}})


def test_other_role_without_flags_is_untouched():
    build_allocs.require_system_config_flags({"name": "L2ValidatorSet"})


def test_c2_layout_address_is_not_special():
    # The committed C2 layout's SystemConfig address: refused like the template's.
    with pytest.raises(ValueError, match="feature_flags"):
        build_allocs.require_system_config_flags(
            {"name": "SystemConfig",
             "address": "0x4200000000000000000000000000000000001000"})


def test_raw_storage_collision_with_oz_owner_is_refused():
    storage = {build_allocs.OZ_OWNER_SLOT: 1}
    # YAML strings parse as hex (word_int): slot 51 arrives as "0x33".
    with pytest.raises(ValueError, match="collides with a computed slot"):
        build_allocs.apply_raw_storage_overlay(
            storage, {"0x33": "0x2"}, "SystemConfig")


def test_raw_storage_collision_with_eip1967_admin_is_refused():
    storage = {build_allocs.EIP1967_ADMIN_SLOT: 1}
    with pytest.raises(ValueError, match="collides"):
        build_allocs.apply_raw_storage_overlay(
            storage, {"0x%064x" % build_allocs.EIP1967_ADMIN_SLOT: "0x2"}, "L1Block")


def test_raw_storage_collision_with_system_config_slot_is_refused():
    computed = build_allocs.system_config_entry_storage({"feature_flags": "0x1"})
    with pytest.raises(ValueError, match="collides"):
        build_allocs.apply_raw_storage_overlay(
            dict(computed), {next(iter(computed)): "0xdead"}, "SystemConfig")


def test_raw_storage_noncolliding_slot_passes_and_stores():
    storage = {build_allocs.OZ_OWNER_SLOT: 1}
    build_allocs.apply_raw_storage_overlay(storage, {"0x1234": "0xabc"}, "L1Block")
    assert storage[0x1234] == 0xABC


def test_raw_storage_quoted_decimal_is_strict_prefix():
    # A quoted "100" is a strict-prefix error, not silently re-read as hex.
    with pytest.raises(ValueError):
        build_allocs.apply_raw_storage_overlay({}, {"0x1": "100"}, "L1Block")
