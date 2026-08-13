# Copyright (c) FISCO-BCOS, Apache-2.0
"""Unit tests for gen_rollup_config.py: the emitted rollup.json must carry every
field op-node v1.19.3 rollup.Config.Check() validates, with all fork times 0
(post-Karst genesis) and a non-empty chain_op_config."""
import importlib.util
import json
from pathlib import Path

import pytest

# Load by file path (same as test_build_allocs.py): tools/opstack-genesis/ is not a
# package, so a plain `import gen_rollup_config` only works under pytest's default
# rootdir sys.path insertion — path loading keeps the test import-mode independent.
_SPEC = importlib.util.spec_from_file_location(
    "gen_rollup_config", str(Path(__file__).parent / "gen_rollup_config.py"))
gen = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(gen)

L1_HASH = "0x" + "11" * 32
L2_HASH = "0x" + "22" * 32


def _args(**overrides):
    argv = [
        "--l1-chain-id", overrides.pop("l1_chain_id", "900"),
        "--l2-chain-id", overrides.pop("l2_chain_id", "901"),
        "--l1-genesis-hash", overrides.pop("l1_genesis_hash", L1_HASH),
        "--l2-genesis-hash", overrides.pop("l2_genesis_hash", L2_HASH),
        "--l2-genesis-time", overrides.pop("l2_genesis_time", "1700000000"),
    ]
    for key, value in overrides.items():
        argv += ["--" + key.replace("_", "-"), str(value)]
    return gen.parse_args(argv)


def test_default_config_passes_check_and_serializes():
    config = gen.check(gen.build_rollup_config(_args()))
    text = json.dumps(config)
    round_tripped = json.loads(text)
    assert round_tripped["l1_chain_id"] == 900
    assert round_tripped["l2_chain_id"] == 901
    assert round_tripped["genesis"]["l1"]["hash"] == L1_HASH
    assert round_tripped["genesis"]["l2"]["hash"] == L2_HASH
    assert round_tripped["genesis"]["l2_time"] == 1700000000
    # the four Check()-relevant system_config keys, camelCase per op-service/eth
    system_config = round_tripped["genesis"]["system_config"]
    assert set(system_config) == {"batcherAddr", "overhead", "scalar", "gasLimit"}
    assert int(system_config["scalar"], 16) != 0
    assert system_config["gasLimit"] == 30_000_000


def test_all_fork_times_zero_through_karst():
    config = gen.check(gen.build_rollup_config(_args()))
    for key in gen.FORK_TIME_KEYS:
        assert config[key] == 0, key
    assert "karst_time" in gen.FORK_TIME_KEYS
    assert gen.FORK_TIME_KEYS[-1] == "karst_time"


def test_chain_op_config_non_empty():
    config = gen.check(gen.build_rollup_config(_args()))
    op_config = config["chain_op_config"]
    assert op_config["eip1559Elasticity"] > 0
    assert op_config["eip1559Denominator"] > 0
    assert op_config["eip1559DenominatorCanyon"] > 0


def test_equal_chain_ids_rejected():
    with pytest.raises(gen.RollupConfigError, match="must differ"):
        gen.check(gen.build_rollup_config(_args(l2_chain_id="900")))


def test_seq_window_size_below_two_rejected():
    with pytest.raises(gen.RollupConfigError, match="seq_window_size"):
        gen.check(gen.build_rollup_config(_args(seq_window_size=1)))


def test_zero_l2_time_rejected():
    with pytest.raises(gen.RollupConfigError, match="l2_time"):
        gen.check(gen.build_rollup_config(_args(l2_genesis_time="0")))


def test_zero_deposit_contract_rejected():
    with pytest.raises(gen.RollupConfigError, match="deposit_contract_address"):
        gen.check(gen.build_rollup_config(
            _args(deposit_contract_address=gen.ZERO_ADDRESS)))


def test_equal_genesis_hashes_rejected():
    with pytest.raises(gen.RollupConfigError, match="genesis hashes"):
        gen.check(gen.build_rollup_config(_args(l2_genesis_hash=L1_HASH)))


def test_malformed_hash_rejected():
    with pytest.raises(gen.RollupConfigError, match="32-byte hex"):
        gen.build_rollup_config(_args(l2_genesis_hash="0x1234"))


def test_main_writes_file(tmp_path, capsys):
    out = tmp_path / "rollup.json"
    rc = gen.main([
        "--l1-chain-id", "900", "--l2-chain-id", "901",
        "--l1-genesis-hash", L1_HASH, "--l2-genesis-hash", L2_HASH,
        "--l2-genesis-time", "1700000000", "--out", str(out),
    ])
    assert rc == 0
    config = json.loads(out.read_text())
    assert config["chain_op_config"]["eip1559Denominator"] == 50


def test_main_rejects_bad_config_with_nonzero_exit(capsys):
    rc = gen.main([
        "--l1-chain-id", "900", "--l2-chain-id", "900",
        "--l1-genesis-hash", L1_HASH, "--l2-genesis-hash", L2_HASH,
        "--l2-genesis-time", "1700000000",
    ])
    assert rc == 1
    assert "must differ" in capsys.readouterr().err


def test_zero_overhead_accepted():
    """Zero overhead must PASS check(): overhead is a pre-Ecotone L1-fee parameter
    (deprecated at Ecotone) and op-node v1.19.3 Config.Check() (rollup/types.go:317
    at tag op-node/v1.19.3) validates no overhead field. Verified against a real
    op-node v1.19.3: a zero-overhead rollup.json passes config validation and
    reaches L1 dialing, while a zero scalar dies there ("missing genesis system
    config scalar"). This test pins that fact - do not "fix" the default to a
    non-zero magic value or add a zero-overhead rejection: post-Ecotone chains
    ship overhead 0x00...00."""
    config = gen.build_rollup_config(_args())
    assert config["genesis"]["system_config"]["overhead"] == "0x" + "00" * 32
    gen.check(config)  # must not raise
