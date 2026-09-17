// run_opgeth — Task 4: op-geth DA/operator-fee matrix runner.
//
// Reads the DA/operator-fee parameter grid (da-matrix/da_matrix.json) and, for
// every case, computes the L1 data fee and the charged operator fee using the
// authoritative op-geth implementation (core/types/rollup_cost.go), producing
// out_opgeth.json as an array of {id, l1_cost, operator_cost}. All costs are
// emitted as lowercase "0x" hex (the op-geth hexutil.Big convention, i.e. the
// same shape run_fisco produces so Task 6 can diff the two ends).
//
// The operator cost MUST go through types.NewOperatorCostFunc — which mirrors
// the FISCO computeChargedOperatorCost gate: it returns 0 for any pre-Isthmus
// block and for a zero operatorFeeParams slot, and selects the Jovian
// operator-fee-fix path via IsJovian (never KarstTime).

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/params"
)

// defaultGrid is the shared grid location (written by Task 1 in the
// op-alignment worktree). Override with --grid.
const defaultGrid = "/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/opstack-executor/tests/da-matrix/da_matrix.json"

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
		// JovianTime deliberately stays 0 (see doc comment). Setting KarstTime
		// to 0 is a no-op for the cost functions but keeps the tag explicit.
	default:
		fmt.Fprintf(os.Stderr, "run_opgeth: unknown fork %q\n", fork)
		os.Exit(2)
	}
	return &cfg
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
	gridPath := flag.String("grid", defaultGrid, "path to da_matrix.json grid")
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
			fmt.Fprintf(os.Stderr, "run_opgeth: case %q got a nil cost func (Optimism not configured)\n", c.ID)
			os.Exit(1)
		}

		rcd := types.NewRollupCostData(envBytes)
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
