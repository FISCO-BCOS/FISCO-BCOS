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
