// Copyright (c) 2026 FISCO BCOS.
// SPDX-License-Identifier: Apache-2.0
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// @file main.go

// run_opgeth — Task 4: op-geth DA/operator-fee matrix runner.
//
// Reads the DA/operator-fee parameter grid (da-matrix/da_matrix.json) and, for
// every case, computes the L1 data fee and the charged operator fee using the
// authoritative op-geth implementation (core/types/rollup_cost.go), producing
// out_opgeth.json as an array of {id, l1_cost, operator_cost}. All costs are
// emitted as lowercase "0x" hex (the op-geth hexutil.Big convention, i.e. the
// same shape run_fisco produces so Task 6 can diff the two ends).
//
// The operator cost MUST go through types.NewOperatorCostFunc — whose gate
// mirrors the FISCO seam: computeOperatorCost (opstack-executor/OpRollupCost.h)
// with the callers applying cfg.has_operator_fee (the block-transition gate,
// OpTransition.cpp:395), never a raw ungated path. It returns 0 for any
// pre-Isthmus block and for a zero operatorFeeParams slot, and selects the
// Jovian operator-fee-fix path via IsJovian (never KarstTime).

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/params"
)

// defaultGrid is the shared grid location, relative to run_opgeth/. Override with --grid.
const defaultGrid = "../da_matrix.json"

// resolveDefaultGrid prefers the CWD-relative grid (the documented invocation is
// `cd run_opgeth && go run .`), then falls back to the runner binary's directory —
// `go run .` executes a temp-dir binary, so the exe anchor alone can never find the
// grid under the documented flow; it only serves a binary built in place.
func resolveDefaultGrid() string {
	if _, err := os.Stat(defaultGrid); err == nil {
		return defaultGrid
	}
	if exe, err := os.Executable(); err == nil {
		anchored := filepath.Join(filepath.Dir(exe), defaultGrid)
		if _, err := os.Stat(anchored); err == nil {
			return anchored
		}
	}
	return defaultGrid
}

// l1BlockAddr is the L1Block contract whose storage holds the L1 gas
// attributes (op-geth core/types/rollup_cost.go).
var l1BlockAddr = common.HexToAddress("0x4200000000000000000000000000000000000015")

// slotKeys maps the grid's slot numbers to the op-geth storage keys.
var slotKeys = map[string]common.Hash{
	"1": types.L1BaseFeeSlot,
	"3": types.L1FeeScalarsSlot,
	"7": types.L1BlobBaseFeeSlot,
	"8": types.OperatorFeeParamsSlot,
}

// stateGetter is a minimal mock of the L1Block contract state. It serves only
// the four storage slots the rollup cost functions read (1/3/7/8) and returns
// the zero hash for any other key, matching the grid's contract.
type stateGetter map[common.Hash]common.Hash

func (s stateGetter) GetState(_ common.Address, key common.Hash) common.Hash {
	if v, ok := s[key]; ok {
		return v
	}
	return common.Hash{}
}

// cfgFor maps a grid fork tag to a *params.ChainConfig with exactly the forks
// up to and including the target activated at time 0 (all prior fork
// timestamps 0, all later forks nil), modeled on params.OptimismTestConfig.
//
// Notes:
//   - jovian (and karst) MUST keep IsthmusTime AND JovianTime at 0: the op-geth
//     operator cost func gates the whole operator-fee path on IsOptimismIsthmus
//     and selects the fee-fix on IsJovian; it never consults KarstTime.
//   - turning a fork off also nils its Ethereum-implication pair so the config
//     still satisfies CheckOptimismValidity (PragueTime == IsthmusTime).
func cfgFor(fork string) *params.ChainConfig {
	cfg := *params.OptimismTestConfig // shallow copy; only reassign pointer fields below
	off := func(dst **uint64) { *dst = nil }
	switch fork {
	case "ecotone":
		off(&cfg.FjordTime)
		off(&cfg.GraniteTime)
		off(&cfg.HoloceneTime)
		off(&cfg.IsthmusTime)
		cfg.PragueTime = nil
		off(&cfg.JovianTime)
		off(&cfg.KarstTime)
	case "fjord":
		off(&cfg.GraniteTime)
		off(&cfg.HoloceneTime)
		off(&cfg.IsthmusTime)
		cfg.PragueTime = nil
		off(&cfg.JovianTime)
		off(&cfg.KarstTime)
	case "granite":
		off(&cfg.HoloceneTime)
		off(&cfg.IsthmusTime)
		cfg.PragueTime = nil
		off(&cfg.JovianTime)
		off(&cfg.KarstTime)
	case "holocene":
		off(&cfg.IsthmusTime)
		cfg.PragueTime = nil
		off(&cfg.JovianTime)
		off(&cfg.KarstTime)
	case "isthmus":
		off(&cfg.JovianTime)
		off(&cfg.KarstTime)
	case "jovian":
		off(&cfg.KarstTime)
	case "karst":
		// JovianTime deliberately stays 0 (see doc comment); KarstTime keeps its
		// zero default here — the case exists to name the tag, not to set a field.
	default:
		fmt.Fprintf(os.Stderr, "run_opgeth: unknown fork %q\n", fork)
		os.Exit(2)
	}
	return &cfg
}

// knownDivergencesPath resolves the canonical tag registry (review F17) beside the
// grid: the grid names tags, known_divergences.json defines them, and this runner,
// compare_snapshots.py and OpDaMatrixSchemaTest.cpp must all agree on the set. The
// file used to be an inline map here — four disagreeing copies, one of which
// rejected a tag the prose registry tells future rows to carry.
func knownDivergencesPath(gridPath string) string {
	return filepath.Join(filepath.Dir(gridPath), "known_divergences.json")
}

// loadKnownTags reads the canonical registry. A missing or malformed file is FATAL
// (loudly, with the path): silently falling back to a built-in list would recreate
// the very drift this registry exists to prevent.
func loadKnownTags(gridPath string) (map[string]bool, error) {
	path := knownDivergencesPath(gridPath)
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("cannot read the canonical known_divergence registry: %w", err)
	}
	var tags []string
	if err := json.Unmarshal(raw, &tags); err != nil {
		return nil, fmt.Errorf("malformed canonical known_divergence registry %s: %w", path, err)
	}
	if len(tags) == 0 {
		return nil, fmt.Errorf("canonical known_divergence registry %s is empty", path)
	}
	known := make(map[string]bool, len(tags))
	for _, tag := range tags {
		if tag == "" {
			return nil, fmt.Errorf("canonical known_divergence registry %s carries an empty tag", path)
		}
		known[tag] = true
	}
	return known, nil
}

type grid struct {
	SchemaVersion int               `json:"schema_version"`
	Envelopes     map[string]string `json:"envelopes"`
	Cases         []gridCase        `json:"cases"`
}

type gridCase struct {
	ID              string            `json:"id"`
	Slots           map[string]string `json:"slots"`
	EnvelopeRef     string            `json:"envelope_ref"`
	Gas             uint64            `json:"gas"`
	BlockTime       uint64            `json:"block_time"`
	Fork            string            `json:"fork"`
	KnownDivergence *string           `json:"known_divergence"`
}

type outItem struct {
	ID           string `json:"id"`
	L1Cost       string `json:"l1_cost"`
	OperatorCost string `json:"operator_cost"`
}

func main() {
	gridPath := flag.String("grid", resolveDefaultGrid(), "path to da_matrix.json grid")
	outPath := flag.String("out", "out_opgeth.json", "output snapshot path")
	flag.Parse()

	raw, err := os.ReadFile(*gridPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "run_opgeth: cannot read grid: %v\n", err)
		os.Exit(1)
	}
	var g grid
	if err := json.Unmarshal(raw, &g); err != nil {
		fmt.Fprintf(os.Stderr, "run_opgeth: JSON parse failed: %v\n", err)
		os.Exit(1)
	}
	if g.Envelopes == nil || g.Cases == nil {
		fmt.Fprintln(os.Stderr, "run_opgeth: grid must carry an 'envelopes' object and a 'cases' array")
		os.Exit(1)
	}
	// A known_divergence tag must be registered — the field used to be
	// parsed and never read, so a typo would silently exempt nothing while the registry
	// claimed the row was registered. The tag set is read at runtime from
	// known_divergences.json (review F17), the single canonical list also consumed by
	// compare_snapshots.py and mirrored by OpDaMatrixSchemaTest.cpp.
	knownTags, err := loadKnownTags(*gridPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "run_opgeth: %v\n", err)
		os.Exit(1)
	}
	seenIDs := make(map[string]bool, len(g.Cases))
	for _, c := range g.Cases {
		if seenIDs[c.ID] {
			fmt.Fprintf(os.Stderr, "run_opgeth: duplicate case id %q\n", c.ID)
			os.Exit(1)
		}
		seenIDs[c.ID] = true
		if c.KnownDivergence != nil && !knownTags[*c.KnownDivergence] {
			fmt.Fprintf(os.Stderr, "run_opgeth: case %q carries unknown known_divergence %q (register it in %s)\n",
				c.ID, *c.KnownDivergence, knownDivergencesPath(*gridPath))
			os.Exit(1)
		}
	}

	out := make([]outItem, 0, len(g.Cases))
	for _, c := range g.Cases {
		envHex, ok := g.Envelopes[c.EnvelopeRef]
		if !ok {
			fmt.Fprintf(os.Stderr, "run_opgeth: case %q unknown envelope_ref %q\n", c.ID, c.EnvelopeRef)
			os.Exit(1)
		}
		envBytes := common.FromHex(envHex)
		if envBytes == nil {
			fmt.Fprintf(os.Stderr, "run_opgeth: case %q envelope is not valid hex\n", c.ID)
			os.Exit(1)
		}

		sg := make(stateGetter)
		for slotNum, valHex := range c.Slots {
			key, known := slotKeys[slotNum]
			if !known {
				fmt.Fprintf(os.Stderr, "run_opgeth: case %q unexpected slot %q (ignored)\n", c.ID, slotNum)
				continue
			}
			sg[key] = common.HexToHash(valHex)
		}

		cfg := cfgFor(c.Fork)
		l1 := types.NewL1CostFunc(cfg, sg)
		op := types.NewOperatorCostFunc(cfg, sg)
		if l1 == nil || op == nil {
			fmt.Fprintf(os.Stderr,
				"run_opgeth: case %q got a nil cost func (Optimism not configured)\n", c.ID)
			os.Exit(1)
		}

		rcd := types.NewRollupCostData(envBytes)
		// op-geth is the only da-matrix end whose cost functions take the block time:
		// it gates Ecotone/Fjord/Isthmus/Jovian and the Regolith L1 path inside the
		// returned funcs. FISCO/op-revm/Solidity are keyed by the grid's fork tag
		// instead; every row pins block_time 0, so the selections agree (review F48,
		// README "Grid input contract").
		l1Cost := l1(rcd, c.BlockTime)
		opCost := op(c.Gas, c.BlockTime)

		l1Hex := "0x0"
		if l1Cost != nil {
			l1Hex = "0x" + l1Cost.Text(16) // Text(16) is lowercase, no "0x"
		}
		item := outItem{ID: c.ID, L1Cost: l1Hex, OperatorCost: opCost.Hex()}
		out = append(out, item)

		fmt.Printf("%s\tl1=%s\top=%s\n", c.ID, item.L1Cost, item.OperatorCost)
	}

	enc, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		fmt.Fprintf(os.Stderr, "run_opgeth: failed to serialize output: %v\n", err)
		os.Exit(1)
	}
	if err := os.WriteFile(*outPath, append(enc, '\n'), 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "run_opgeth: cannot write output: %v\n", err)
		os.Exit(1)
	}
}
