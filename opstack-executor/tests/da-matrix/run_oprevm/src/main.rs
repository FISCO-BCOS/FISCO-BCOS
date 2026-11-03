// run_oprevm — op-revm 20.0.0 DA/operator-fee matrix runner.
//
// Reads the DA/operator-fee parameter grid (da-matrix/da_matrix.json), computes
// the L1 data fee and the charged operator fee for every case via op-revm's
// L1BlockInfo, and writes out_oprevm.json as an array of
// {id, l1_cost, operator_cost}. All costs are emitted as lowercase "0x" hex,
// the op-geth hexutil.Big convention (no leading zeros) — matching the FISCO
// runner and the Global Constraints of the da-matrix plan.
//
// Reference API (op-revm 20.0.0, src/l1block.rs):
//   - operator_fee_charge(&self, input: &[u8], gas_limit: U256, spec) :174
//   - calculate_tx_l1_cost(&mut self, input: &[u8], spec)            :286
//   - tx_cost(&mut self, enveloped_tx, gas_limit, spec)              :271
//
// The operator fee is gated on Isthmus exactly like tx_cost() does internally
// (`if spec.is_enabled_in(OpSpecId::ISTHMUS)`) so a pre-Isthmus fork
// (ecotone/fjord/granite/holocene) reports 0 even with a non-zero scalar/
// constant packed in slot 8 — mirroring the FISCO computeChargedOperatorCost
// gate (OpTransition.cpp:395).
//
// OpSpecId::from_str is case-sensitive ("Ecotone".."Karst", spec.rs:63-87) and
// the grid tags are lowercase, so we capitalize the first letter before parse.

use op_revm::L1BlockInfo;
use op_revm::OpSpecId;
use op_revm::revm::primitives::U256;
use serde_json::{Value, json};
use std::env;
use std::fs;
use std::process;

const DEFAULT_GRID: &str = "/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/opstack-executor/tests/da-matrix/da_matrix.json";
const DEFAULT_OUT: &str = "out_oprevm.json";

/// "0x"-prefixed or bare lowercase/uppercase hex -> bytes; None on malformed input.
fn hex_decode(s: &str) -> Option<Vec<u8>> {
    let s = s.strip_prefix("0x").unwrap_or(s);
    if s.len() % 2 != 0 {
        return None;
    }
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len() / 2);
    for i in (0..bytes.len()).step_by(2) {
        let hi = (bytes[i] as char).to_digit(16)?;
        let lo = (bytes[i + 1] as char).to_digit(16)?;
        out.push(((hi << 4) | lo) as u8);
    }
    Some(out)
}

/// Exactly-32-byte hex -> [u8; 32]; None on malformed/wrong-length input.
fn bytes32_from_hex(s: &str) -> Option<[u8; 32]> {
    let v = hex_decode(s)?;
    if v.len() != 32 {
        return None;
    }
    let mut out = [0u8; 32];
    out.copy_from_slice(&v);
    Some(out)
}

/// Grid fork tag (lowercase) -> OpSpecId (case-sensitive "Ecotone".."Karst").
fn fork_to_spec(fork: &str) -> Option<OpSpecId> {
    let mut chars = fork.chars();
    let first = chars.next()?;
    let mut cap = String::new();
    cap.push(first.to_ascii_uppercase());
    cap.push_str(chars.as_str());
    cap.parse::<OpSpecId>().ok()
}

fn get_slot<'a>(slots: &'a serde_json::Map<String, Value>, key: &str, id: &str) -> [u8; 32] {
    match slots.get(key).and_then(|v| v.as_str()) {
        Some(hex) => match bytes32_from_hex(hex) {
            Some(b) => b,
            None => {
                eprintln!("run_oprevm: case '{}' slot '{}' is not valid 32-byte hex", id, key);
                process::exit(1);
            }
        },
        None => {
            eprintln!("run_oprevm: case '{}' missing slot '{}'", id, key);
            process::exit(1);
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let mut grid_path = DEFAULT_GRID.to_string();
    let mut out_path = DEFAULT_OUT.to_string();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--grid" if i + 1 < args.len() => {
                grid_path = args[i + 1].clone();
                i += 2;
            }
            "--out" if i + 1 < args.len() => {
                out_path = args[i + 1].clone();
                i += 2;
            }
            _ => {
                eprintln!("usage: run_oprevm [--grid <da_matrix.json>] [--out <out.json>]");
                process::exit(2);
            }
        }
    }

    let grid_text = match fs::read_to_string(&grid_path) {
        Ok(t) => t,
        Err(e) => {
            eprintln!("run_oprevm: cannot open grid {}: {}", grid_path, e);
            process::exit(1);
        }
    };
    let root: Value = match serde_json::from_str(&grid_text) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("run_oprevm: JSON parse failed: {}", e);
            process::exit(1);
        }
    };

    let envelopes = root.get("envelopes").and_then(|v| v.as_object());
    let cases = root.get("cases").and_then(|v| v.as_array());
    let (Some(envelopes), Some(cases)) = (envelopes, cases) else {
        eprintln!("run_oprevm: grid must carry an 'envelopes' object and a 'cases' array");
        process::exit(1);
    };

    let mut out = Vec::new();
    for c in cases {
        let id = c.get("id").and_then(|v| v.as_str()).unwrap_or("").to_string();
        let env_ref = c.get("envelope_ref").and_then(|v| v.as_str()).unwrap_or("");
        let envelope_hex = match envelopes.get(env_ref).and_then(|v| v.as_str()) {
            Some(h) => h,
            None => {
                eprintln!("run_oprevm: case '{}' unknown envelope_ref '{}'", id, env_ref);
                process::exit(1);
            }
        };
        let envelope = match hex_decode(envelope_hex) {
            Some(e) => e,
            None => {
                eprintln!("run_oprevm: case '{}' envelope is not valid hex", id);
                process::exit(1);
            }
        };

        // gas: as_u64 — the overflow rows carry gas=2^64-1; serde_json as_u64
        // preserves it exactly (as_f64 would round to 2^64).
        let gas = match c.get("gas").and_then(|v| v.as_u64()) {
            Some(g) => g,
            None => {
                eprintln!("run_oprevm: case '{}' has no u64 'gas'", id);
                process::exit(1);
            }
        };

        let fork = c.get("fork").and_then(|v| v.as_str()).unwrap_or("");
        let spec = match fork_to_spec(fork) {
            Some(s) => s,
            None => {
                eprintln!("run_oprevm: case '{}' unknown fork '{}'", id, fork);
                process::exit(1);
            }
        };

        let slots = match c.get("slots").and_then(|v| v.as_object()) {
            Some(s) => s,
            None => {
                eprintln!("run_oprevm: case '{}' has no 'slots' object", id);
                process::exit(1);
            }
        };
        let slot1 = get_slot(slots, "1", &id);
        let slot3 = get_slot(slots, "3", &id);
        let slot7 = get_slot(slots, "7", &id);
        let slot8 = get_slot(slots, "8", &id);

        // Build L1BlockInfo from the grid slots, mirroring the op-revm
        // try_fetch layout (constants.rs):
        //   slot1            -> l1_base_fee            (L1_BASE_FEE_SLOT)
        //   slot3[16..20)    -> l1_base_fee_scalar     (BASE_FEE_SCALAR_OFFSET)
        //   slot3[20..24)    -> l1_blob_base_fee_scalar(BLOB_BASE_FEE_SCALAR_OFFSET)
        //   slot7            -> l1_blob_base_fee       (ECOTONE_L1_BLOB_BASE_FEE_SLOT)
        //   slot8[18..20)    -> da_footprint_gas_scalar(DA_FOOTPRINT_GAS_SCALAR_OFFSET)
        //   slot8[20..24)    -> operator_fee_scalar    (OPERATOR_FEE_SCALAR_OFFSET)
        //   slot8[24..32)    -> operator_fee_constant  (OPERATOR_FEE_CONSTANT_OFFSET)
        let mut info = L1BlockInfo {
            l1_base_fee: U256::from_be_slice(&slot1),
            l1_base_fee_scalar: U256::from_be_slice(&slot3[16..20]),
            l1_blob_base_fee: Some(U256::from_be_slice(&slot7)),
            l1_blob_base_fee_scalar: Some(U256::from_be_slice(&slot3[20..24])),
            operator_fee_scalar: Some(U256::from_be_slice(&slot8[20..24])),
            operator_fee_constant: Some(U256::from_be_slice(&slot8[24..32])),
            da_footprint_gas_scalar: Some(u16::from_be_bytes([slot8[18], slot8[19]])),
            // empty_ecotone_scalars: blobBaseFee == 0 && slot3 scalars all zero
            // (l1block.rs try_fetch_ecotone). Never true for the current grid.
            empty_ecotone_scalars: U256::from_be_slice(&slot7).is_zero()
                && slot3[16..24].iter().all(|&b| b == 0),
            ..Default::default()
        };

        let l1_cost = info.calculate_tx_l1_cost(&envelope, spec);
        let operator_cost = if spec.is_enabled_in(OpSpecId::ISTHMUS) {
            info.operator_fee_charge(&envelope, U256::from(gas), spec)
        } else {
            U256::ZERO
        };

        let l1_hex = format!("0x{:x}", l1_cost);
        let op_hex = format!("0x{:x}", operator_cost);
        println!("{}\tl1={}\top={}", id, l1_hex, op_hex);
        out.push(json!({
            "id": id,
            "l1_cost": l1_hex,
            "operator_cost": op_hex,
        }));
    }

    let out_json = match serde_json::to_string_pretty(&Value::Array(out)) {
        Ok(j) => j,
        Err(e) => {
            eprintln!("run_oprevm: failed to serialize output: {}", e);
            process::exit(1);
        }
    };
    if let Err(e) = fs::write(&out_path, out_json + "\n") {
        eprintln!("run_oprevm: cannot write output {}: {}", out_path, e);
        process::exit(1);
    }
}
