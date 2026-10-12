# Copyright (c) 2026 FISCO BCOS.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
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
    # A quoted "100" is a strict-prefix error, not silently re-read as hex — on the
    # VALUE lane and on the KEY lane alike (a quoted key would root slot 0x100=256
    # instead of the 100 the config meant).
    with pytest.raises(ValueError):
        build_allocs.apply_raw_storage_overlay({}, {"0x1": "100"}, "L1Block")
    with pytest.raises(ValueError):
        build_allocs.apply_raw_storage_overlay({}, {"100": "0x1"}, "L1Block")
    with pytest.raises(ValueError):
        build_allocs.apply_raw_storage_overlay({}, {"-1": "0x1"}, "L1Block")


def test_raw_storage_key_lane_accepts_hex_and_int():
    # The strict rule does not break the two legitimate spellings: an explicit 0x key
    # and an unquoted decimal key (YAML parses the latter into an int).
    storage = {}
    build_allocs.apply_raw_storage_overlay(storage, {"0x64": "0x1"}, "L1Block")
    assert storage[100] == 1
    build_allocs.apply_raw_storage_overlay(storage, {200: "0x2"}, "L1Block")
    assert storage[200] == 2


def test_raw_storage_negative_int_rejected():
    # An unquoted negative (a YAML int) must not reach the emitter: word_hex would
    # render it as a sign-prefixed "hex" word ("0x-000...01") in the INI and the
    # alloc JSON. The string spelling "-1" was already rejected; this pins the
    # int form on both lanes (key and value).
    with pytest.raises(ValueError):
        build_allocs.apply_raw_storage_overlay({}, {-1: "0x1"}, "L1Block")
    with pytest.raises(ValueError):
        build_allocs.apply_raw_storage_overlay({}, {"0x1": -1}, "L1Block")


def test_chain_config_duplicate_yaml_key_rejected():
    # The CLI parses the (hand-edited) chain config with the same duplicate-key
    # rule as the op-deployer JSON loader: yaml.safe_load would silently keep the
    # LAST value and drop the earlier, reviewed one.
    import io

    import yaml
    text = "governance_owner:\n  x: 1\ngovernance_owner:\n  x: 2\n"
    with pytest.raises(ValueError):
        yaml.load(io.StringIO(text), Loader=build_allocs._DuplicateRejectingSafeLoader)
    # The strict loader otherwise behaves exactly like SafeLoader.
    assert yaml.load(io.StringIO("a: 1\nb: [2, 3]\n"),
        Loader=build_allocs._DuplicateRejectingSafeLoader) == {"a": 1, "b": [2, 3]}


def test_base_allocs_duplicate_json_key_is_refused(tmp_path):
    # json.load keeps the last value for a repeated key, so the address-level
    # duplicate guard never saw it; the object_pairs_hook surfaces it at parse time.
    path = tmp_path / "allocs.json"
    path.write_text(
        '{"0x00000000000000000000000000000000000000ab": {"balance": "0x1"},'
        ' "0x00000000000000000000000000000000000000ab": {"balance": "0x2"}}')
    with pytest.raises(ValueError, match="duplicate JSON key"):
        build_allocs.load_base_allocs(str(path))
