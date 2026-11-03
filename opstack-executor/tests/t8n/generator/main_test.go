package main

// Task 3/4 TDD tests. ⚠️ Every test references at least one function that is
// UNDEFINED before the implementation lands (buildBlockVector / corruptVector /
// captureInsertChainRejection / recomputeOpHeaderHash / buildCaseFromSpecs /
// emitStaticSurfaceVector / staticSurfaceItems / legacy arm for Task 3;
// buildInvalidTxCase / buildInvalidTxBlock / buildInvalidTxVector /
// invalidTxCaseSpecs / invalidTxSpec / emitableSpecs for Task 4). Before the
// implementation steps this file fails to COMPILE (real red); after
// implementation it goes green.

import (
	"encoding/json"
	"math/big"
	"strings"
	"testing"

	"github.com/ethereum/go-ethereum/consensus/beacon"
	"github.com/ethereum/go-ethereum/consensus/ethash"
	"github.com/ethereum/go-ethereum/core"
	"github.com/ethereum/go-ethereum/core/rawdb"
	"github.com/ethereum/go-ethereum/core/txpool"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/trie"
)

// rejectOf is a small accessor: returns the reject schema or nil.
func rejectOf(doc invalidVectorDoc) *rejectExpected {
	return doc.OpExpected.Reject
}

func TestCorruptStateRootRejectedByOpGeth(t *testing.T) {
	in, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs: %v", err)
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		t.Fatalf("buildBlockVector: %v", err)
	}
	doc, corruptBlock, err := corruptVector(base, "stateRoot")
	if err != nil {
		t.Fatalf("corruptVector: %v", err)
	}
	// Iron-rule mirror: the self-consistent corrupt block MUST be rejected by
	// op-geth's own InsertChain (fresh DB, same genesis).
	msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
	if err != nil {
		t.Fatalf("expected reject, got accept: %v", err)
	}
	if !strings.Contains(msg, "invalid merkle root") {
		t.Fatalf("want 'invalid merkle root', got %q", msg)
	}
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("corrupt vector missing _op_expected.reject")
	}
	if rej.Fisco.Classification != "INVALID" {
		t.Fatalf("classification want INVALID, got %q", rej.Fisco.Classification)
	}
	if rej.Fisco.Consumer != "engine" {
		t.Fatalf("consumer want engine, got %q", rej.Fisco.Consumer)
	}
	if !strings.Contains(rej.Fisco.ValidationErrorContains, "stateRoot") {
		t.Fatalf("validation_error_contains want stateRoot, got %q", rej.Fisco.ValidationErrorContains)
	}
	// Self-consistent: payload blockHash == hash of the CORRUPT header.
	wantHash := recomputeOpHeaderHash(corruptBlock.Header())
	if doc.OpPayload["blockHash"] != wantHash.Hex() {
		t.Fatalf("payload blockHash %v != recomputed %v", doc.OpPayload["blockHash"], wantHash.Hex())
	}
	if doc.OpPayload["stateRoot"] == base.vec.OpExpected.Header.StateRoot {
		t.Fatal("stateRoot not actually corrupted in payload")
	}
}

func TestCorruptGasUsedRejectedByOpGeth(t *testing.T) {
	in, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs: %v", err)
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		t.Fatalf("buildBlockVector: %v", err)
	}
	doc, corruptBlock, err := corruptVector(base, "gasUsed")
	if err != nil {
		t.Fatalf("corruptVector: %v", err)
	}
	msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
	if err != nil {
		t.Fatalf("expected reject, got accept: %v", err)
	}
	if !strings.Contains(msg, "invalid gas used") {
		t.Fatalf("want 'invalid gas used', got %q", msg)
	}
	rej := rejectOf(doc)
	if rej == nil || !strings.Contains(rej.Fisco.ValidationErrorContains, "gasUsed") {
		t.Fatalf("reject missing gasUsed anchor: %+v", rej)
	}
	// gasUsed must stay <= gasLimit (recipe constraint).
	gasUsed := doc.OpPayload["gasUsed"].(string)
	gasLimit := doc.OpPayload["gasLimit"].(string)
	if gasUsed > gasLimit {
		t.Fatalf("corrupt gasUsed %s > gasLimit %s (must stay <= gasLimit)", gasUsed, gasLimit)
	}
}

func TestCorruptReceiptsRootRejectedByOpGeth(t *testing.T) {
	in, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs: %v", err)
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		t.Fatalf("buildBlockVector: %v", err)
	}
	doc, corruptBlock, err := corruptVector(base, "receiptsRoot")
	if err != nil {
		t.Fatalf("corruptVector: %v", err)
	}
	msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
	if err != nil {
		t.Fatalf("expected reject, got accept: %v", err)
	}
	if !strings.Contains(msg, "invalid receipt root hash") {
		t.Fatalf("want 'invalid receipt root hash', got %q", msg)
	}
	rej := rejectOf(doc)
	if rej == nil || !strings.Contains(rej.Fisco.ValidationErrorContains, "receiptsRoot") {
		t.Fatalf("reject missing receiptsRoot anchor: %+v", rej)
	}
}

func TestCorruptParentHashUnknownAncestor(t *testing.T) {
	in, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs: %v", err)
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		t.Fatalf("buildBlockVector: %v", err)
	}
	doc, corruptBlock, err := corruptVector(base, "parentHash")
	if err != nil {
		t.Fatalf("corruptVector: %v", err)
	}
	msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
	if err != nil {
		t.Fatalf("expected reject, got accept: %v", err)
	}
	if !strings.Contains(msg, "unknown ancestor") {
		t.Fatalf("want 'unknown ancestor', got %q", msg)
	}
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("missing reject")
	}
	if rej.Fisco.Classification != "SYNCING" {
		t.Fatalf("parentHash corrupt must classify SYNCING, got %q", rej.Fisco.Classification)
	}
	if len(rej.Fisco.LatestValidHash) != 0 {
		t.Fatalf("SYNCING must omit latest_valid_hash, got %s", rej.Fisco.LatestValidHash)
	}
}

func TestCorruptExtraDataShapeRejected(t *testing.T) {
	in, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs: %v", err)
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		t.Fatalf("buildBlockVector: %v", err)
	}
	doc, corruptBlock, err := corruptVector(base, "extraData")
	if err != nil {
		t.Fatalf("corruptVector: %v", err)
	}
	msg, err := captureInsertChainRejection(base.genesis, corruptBlock)
	if err != nil {
		t.Fatalf("expected reject, got accept: %v", err)
	}
	if !strings.Contains(msg, "invalid optimism extraData") {
		t.Fatalf("want 'invalid optimism extraData', got %q", msg)
	}
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("missing reject")
	}
	// extraData shape break must NOT recompute blockHash: payload carries the
	// ORIGINAL golden blockHash (the static shape check fires before blockHash).
	if doc.OpPayload["blockHash"] != base.golden.BlockHash {
		t.Fatalf("extraData corrupt must keep original blockHash, got %v want %v",
			doc.OpPayload["blockHash"], base.golden.BlockHash)
	}
	if !strings.Contains(rej.Fisco.ValidationErrorContains, "extraData") {
		t.Fatalf("validation_error_contains want extraData substring, got %q", rej.Fisco.ValidationErrorContains)
	}
}

func TestBlockHashCorruptAnchorsBlockhashMismatch(t *testing.T) {
	in, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs: %v", err)
	}
	base, err := buildBlockVector(&in)
	if err != nil {
		t.Fatalf("buildBlockVector: %v", err)
	}
	// blockHash corruption is the EXCEPTION: no InsertChain capture (the block's
	// own hash stays self-consistent), anchored on the engine-API message.
	doc, corruptBlock, err := corruptVector(base, "blockHash")
	if err != nil {
		t.Fatalf("corruptVector: %v", err)
	}
	if corruptBlock != nil {
		t.Fatal("blockHash corrupt must not produce a capture block")
	}
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("missing reject")
	}
	if rej.OpGeth != "blockhash mismatch" {
		t.Fatalf("op_geth want 'blockhash mismatch', got %q", rej.OpGeth)
	}
	if rej.Fisco.Classification != "INVALID" {
		t.Fatalf("classification want INVALID, got %q", rej.Fisco.Classification)
	}
	if string(rej.Fisco.LatestValidHash) != "null" {
		t.Fatalf("blockHash mismatch must be latest_valid_hash null, got %s", rej.Fisco.LatestValidHash)
	}
	// The payload blockHash must actually differ from the golden hash.
	if doc.OpPayload["blockHash"] == base.golden.BlockHash {
		t.Fatal("blockHash not actually corrupted")
	}
}

func TestStaticSurfaceAllItemsCarryFiscoMessage(t *testing.T) {
	isth, err := buildCaseFromSpecs("transfer_basic", "isthmus")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs(isthmus): %v", err)
	}
	jov, err := buildCaseFromSpecs("transfer_basic", "jovian")
	if err != nil {
		t.Fatalf("buildCaseFromSpecs(jovian): %v", err)
	}
	isthBase, err := buildBlockVector(&isth)
	if err != nil {
		t.Fatalf("buildBlockVector(isthmus): %v", err)
	}
	jovBase, err := buildBlockVector(&jov)
	if err != nil {
		t.Fatalf("buildBlockVector(jovian): %v", err)
	}
	if len(staticSurfaceItems) < 12 {
		t.Fatalf("static surface must enumerate 12 items, got %d", len(staticSurfaceItems))
	}
	for _, item := range staticSurfaceItems {
		base := isthBase
		if item.fork == "jovian" {
			base = jovBase
		}
		doc, err := emitStaticSurfaceVector(base, item)
		if err != nil {
			t.Fatalf("emitStaticSurfaceVector(%s): %v", item.name, err)
		}
		rej := rejectOf(doc)
		if rej == nil {
			t.Fatalf("static item %s missing reject", item.name)
		}
		if rej.Fisco.Classification != "INVALID" {
			t.Fatalf("static item %s: classification want INVALID, got %q", item.name, rej.Fisco.Classification)
		}
		if rej.Fisco.ValidationErrorContains != item.fiscoMessage {
			t.Fatalf("static item %s: validation_error_contains %q != fiscoMessage %q",
				item.name, rej.Fisco.ValidationErrorContains, item.fiscoMessage)
		}
		// Static checks fire at validateOpNewPayloadRequest → INVALID + null.
		if string(rej.Fisco.LatestValidHash) != "null" {
			t.Fatalf("static item %s: latest_valid_hash want null, got %s", item.name, rej.Fisco.LatestValidHash)
		}
	}
}

// TestChainPairReplayStartRoot is the Task-5 前置 regression: --chain-output-dir's
// chain-pair block B replay started from the GENESIS root instead of block A's
// post-state root, so block B's transfer (nonce 1) was replayed against nonce 0
// -> "chain block B: replay tx 1: nonce too high". The fix threads a per-block
// start root into reexecuteOutputs (block i replays from blocks[i-1].Root();
// single-block vectors keep the genesis root).
func TestChainPairReplayStartRoot(t *testing.T) {
	for _, fork := range []string{"isthmus", "jovian"} {
		outA, outB, goldenA, goldenB, err := processChainPair(fork)
		if err != nil {
			t.Fatalf("processChainPair(%s): %v", fork, err)
		}
		if goldenA == nil || goldenB == nil {
			t.Fatalf("%s: nil golden record", fork)
		}
		sender := addrOfKey(1)
		// Block A's post-state must carry the spent nonce (the chain-pair
		// output is only trustworthy if the replay reproduced it).
		if accA, ok := outA.PostState[sender]; !ok || accA.Nonce != 1 {
			t.Fatalf("%s: block A postState sender nonce want 1, got %v (present %v)", fork, accA.Nonce, ok)
		}
		// Decision A2: block B's emitted pre IS block A's post-state. Block A's
		// transfer consumed sender nonce 0, so block B's pre must show nonce 1
		// (a genesis-root replay would show 0 -- the pre-existing bug).
		acc, ok := outB.Pre[sender]
		if !ok {
			t.Fatalf("%s: block B pre missing sender %s", fork, sender.Hex())
		}
		if acc.Nonce != 1 {
			t.Fatalf("%s: block B pre sender nonce want 1 (block A spent nonce 0), got %d", fork, acc.Nonce)
		}
	}
}

// ---------------------------------------------------------------------
// Task 5 TDD tests — chain mode (N>=3 linear chain + same-parent fork + parent
// break). ⚠️ Every test references at least one function that is UNDEFINED
// before the implementation lands (processChainN / generateChainN /
// genSiblingFork / buildForkVector / genParentBreak). Before Step 3 this file
// fails to COMPILE (real red); after implementation it goes green.
// ---------------------------------------------------------------------

func TestChain3Blocks(t *testing.T) {
	for _, fork := range []string{"isthmus", "jovian"} {
		out, err := processChainN(fork, 3)
		if err != nil {
			t.Fatalf("processChainN(%s, 3): %v", fork, err)
		}
		if out == nil || len(out.Blocks) != 3 {
			t.Fatalf("%s: chain must have 3 blocks, got %d", fork, len(out.Blocks))
		}
		sender := addrOfKey(1)
		for i, blk := range out.Blocks {
			// Every block must carry its own _op_expected.header + receipts and
			// postState (Task 1 handoff contract -- replaySingleBlockInto reads
			// them with .at(), missing = out_of_range).
			h := blk.OpExpected.Header
			if h.GasUsed == "" || h.ReceiptsRoot == "" || h.StateRoot == "" || h.WithdrawalsRoot == "" {
				t.Fatalf("%s block %d: incomplete header expectation %+v", fork, i, h)
			}
			if len(blk.OpExpected.Receipts) != 2 {
				t.Fatalf("%s block %d: want 2 receipts (deposit + transfer), got %d", fork, i, len(blk.OpExpected.Receipts))
			}
			// State accumulates across the chain: block i's postState carries
			// sender nonce i+1 (block 0 transfer spends nonce 0, block 1 spends 1,
			// block 2 spends 2).
			acc, ok := blk.PostState[sender]
			if !ok {
				t.Fatalf("%s block %d: postState missing sender %s", fork, i, sender.Hex())
			}
			if acc.Nonce != uint64(i+1) {
				t.Fatalf("%s block %d: sender postState nonce want %d, got %d", fork, i, i+1, acc.Nonce)
			}
			// pre present ONLY on block 0 (the replayer inherits the running
			// chain state for blocks i>0).
			if i == 0 {
				if blk.Pre == nil {
					t.Fatalf("%s block 0 must carry pre", fork)
				}
			} else if blk.Pre != nil {
				t.Fatalf("%s block %d must NOT carry pre (replayer inherits chain state)", fork, i)
			}
		}
	}
}

func TestSiblingForkIsDivergence(t *testing.T) {
	// Build the chain to obtain the canonical height-1 child (block 0).
	_, ctx, err := generateChainN("isthmus", 3)
	if err != nil {
		t.Fatalf("generateChainN: %v", err)
	}
	canonical := ctx.blocks[0]
	sibling, err := genSiblingFork(canonical, ctx.cfg, ctx.genesis)
	if err != nil {
		t.Fatalf("genSiblingFork: %v", err)
	}
	// Same parent + same height, different hash.
	if sibling.Hash() == canonical.Hash() {
		t.Fatalf("sibling must diverge from canonical, both %s", canonical.Hash().Hex())
	}
	if sibling.ParentHash() != canonical.ParentHash() {
		t.Fatalf("sibling parent %s != canonical parent %s (must share parent)", sibling.ParentHash().Hex(), canonical.ParentHash().Hex())
	}
	if sibling.NumberU64() != canonical.NumberU64() {
		t.Fatalf("sibling number %d != canonical number %d (must share height)", sibling.NumberU64(), canonical.NumberU64())
	}
	// ⚠️ Minimal experiment (review-required): op-geth InsertChain must ACCEPT
	// the side-chain sibling -- the divergence recorded as "VALID (side chain)".
	// First insert the canonical child, then the sibling as a side chain. (In a
	// plain beacon-engine NewBlockChain with no explicit forkChoiceUpdate, geth
	// moves its local head to the last-inserted same-height block; that internal
	// artifact is NOT asserted -- only the nil acceptance, which is the divergence.)
	engine := beacon.New(ethash.NewFaker())
	chain, err := core.NewBlockChain(rawdb.NewMemoryDatabase(), ctx.genesis, engine, nil)
	if err != nil {
		t.Fatalf("NewBlockChain: %v", err)
	}
	defer chain.Stop()
	if _, err := chain.InsertChain(types.Blocks{canonical}); err != nil {
		t.Fatalf("InsertChain canonical: %v", err)
	}
	if _, err := chain.InsertChain(types.Blocks{sibling}); err != nil {
		t.Fatalf("expected op-geth accept (side chain), got: %v", err)
	}
}

func TestForkCarrierSchema(t *testing.T) {
	_, ctx, err := generateChainN("isthmus", 3)
	if err != nil {
		t.Fatalf("generateChainN: %v", err)
	}
	canonical := ctx.blocks[0]
	sibling, err := genSiblingFork(canonical, ctx.cfg, ctx.genesis)
	if err != nil {
		t.Fatalf("genSiblingFork: %v", err)
	}
	doc, err := buildForkVector("isthmus", canonical, sibling, ctx.genesisPre)
	if err != nil {
		t.Fatalf("buildForkVector: %v", err)
	}
	// Task 2 handoff: the -32603 carrier MUST carry _op_canonical (the E2E
	// runner's two-pour reads it -- OpNewPayloadRpcE2eTest.cpp:563).
	if doc.OpCanonical == nil {
		t.Fatal("fork carrier missing _op_canonical")
	}
	if doc.OpPayload == nil {
		t.Fatal("fork carrier missing _op_payload")
	}
	// _op_canonical must be the canonical child's payload (blockHash matches).
	if ch, ok := doc.OpCanonical["blockHash"].(string); !ok || ch != canonical.Hash().Hex() {
		t.Fatalf("_op_canonical blockHash want %s, got %v", canonical.Hash().Hex(), doc.OpCanonical["blockHash"])
	}
	// _op_payload must be the sibling's payload.
	if ph, ok := doc.OpPayload["blockHash"].(string); !ok || ph != sibling.Hash().Hex() {
		t.Fatalf("_op_payload blockHash want %s, got %v", sibling.Hash().Hex(), doc.OpPayload["blockHash"])
	}
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("fork carrier missing reject")
	}
	if rej.Fisco.Classification != "-32603" {
		t.Fatalf("classification want -32603, got %q", rej.Fisco.Classification)
	}
	if rej.Fisco.ExpectThrow != "OpExecutionInternalError" {
		t.Fatalf("expect_throw want OpExecutionInternalError, got %q", rej.Fisco.ExpectThrow)
	}
	if string(rej.Fisco.LatestValidHash) != "null" {
		t.Fatalf("-32603 latest_valid_hash want null, got %s", rej.Fisco.LatestValidHash)
	}
	if rej.OpGeth != "VALID (side chain)" {
		t.Fatalf("op_geth want 'VALID (side chain)', got %q", rej.OpGeth)
	}
}

func TestParentBreakSyncs(t *testing.T) {
	_, ctx, err := generateChainN("isthmus", 3)
	if err != nil {
		t.Fatalf("generateChainN: %v", err)
	}
	doc, err := genParentBreak("isthmus", ctx.blocks[0], ctx.cfg, ctx.genesis, ctx.genesisPre)
	if err != nil {
		t.Fatalf("genParentBreak: %v", err)
	}
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("break vector missing reject")
	}
	if rej.Fisco.Classification != "SYNCING" {
		t.Fatalf("classification want SYNCING, got %q", rej.Fisco.Classification)
	}
	if rej.Fisco.Consumer != "engine" {
		t.Fatalf("consumer want engine, got %q", rej.Fisco.Consumer)
	}
	if len(rej.Fisco.LatestValidHash) != 0 {
		t.Fatalf("SYNCING must omit latest_valid_hash, got %s", rej.Fisco.LatestValidHash)
	}
	// payload parentHash must be the unknown ancestor (0x...01).
	ph, ok := doc.OpPayload["parentHash"].(string)
	if !ok || ph != "0x0000000000000000000000000000000000000000000000000000000000000001" {
		t.Fatalf("break payload parentHash want unknown ancestor, got %v", ph)
	}
	// op-geth anchor: the real InsertChain "unknown ancestor" rejection.
	if !strings.Contains(rej.OpGeth, "unknown ancestor") {
		t.Fatalf("op_geth want 'unknown ancestor' substring, got %q", rej.OpGeth)
	}
}

func TestRejectFieldOmitEmptyKeepsValidVectorBytes(t *testing.T) {
	// The Reject field must be omitempty: a VALID vector's marshaled bytes must
	// not contain a "reject" key (byte-invariance for the old 77 vectors).
	vec := outputVector{
		Info: caseInfo{Hardfork: "isthmus", Description: "x"},
		OpExpected: opExpected{
			Header:   expectedHeader{},
			Receipts: []expectedReceipt{},
		},
	}
	b, err := json.Marshal(vec)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if strings.Contains(string(b), "reject") {
		t.Fatalf("valid vector must not emit reject key, got %s", b)
	}
}

func TestLegacyTransferCaseSpec(t *testing.T) {
	for _, fork := range []string{"isthmus", "jovian"} {
		in, err := buildCaseFromSpecs("legacy_transfer", fork)
		if err != nil {
			t.Fatalf("buildCaseFromSpecs(legacy_transfer, %s): %v", fork, err)
		}
		// type-0 + EIP-155: the built tx must carry _op_type legacy and a raw
		// envelope that decodes to a protected (EIP-155) legacy tx.
		if len(in.Transactions) != 2 {
			t.Fatalf("%s: legacy_transfer must have deposit + legacy tx, got %d", fork, len(in.Transactions))
		}
		if in.Transactions[1].OpType != "legacy" {
			t.Fatalf("%s: tx1 must be legacy, got %q", fork, in.Transactions[1].OpType)
		}
		base, err := buildBlockVector(&in)
		if err != nil {
			t.Fatalf("buildBlockVector(legacy_transfer, %s): %v", fork, err)
		}
		if len(base.vec.Block.Transactions) != 2 {
			t.Fatalf("%s: output must have 2 transactions, got %d", fork, len(base.vec.Block.Transactions))
		}
		// Valid vector → no reject.
		if base.vec.OpExpected.Reject != nil {
			t.Fatalf("%s: legacy_transfer is a VALID case, must not carry reject", fork)
		}
		// The legacy output object is the second transaction.
		var txObj map[string]interface{}
		if err := json.Unmarshal(base.vec.Block.Transactions[1], &txObj); err != nil {
			t.Fatalf("%s: unmarshal legacy out tx: %v", fork, err)
		}
		if txObj["_op_type"] != "legacy" {
			t.Fatalf("%s: output tx1 _op_type want legacy, got %v", fork, txObj["_op_type"])
		}
		if _, ok := txObj["_op_raw"]; !ok {
			t.Fatalf("%s: legacy output must carry _op_raw", fork)
		}
	}
}

// ---------------------------------------------------------------------
// Task 4 TDD tests — invalid-tx mode. ⚠️ Every test references at least one
// function/type that is UNDEFINED before the implementation lands
// (buildInvalidTxCase / buildInvalidTxBlock / buildInvalidTxVector /
// invalidTxCaseSpecs / emitableSpecs). Before Step 3 this file fails to
// COMPILE (real red); after implementation it goes green.
// ---------------------------------------------------------------------

// invalidTxAnchor is one kind's expected rejection signature: the op-geth
// message substring (weak anchor) and the T8n/executor throw substring
// (validation_error_contains).
type invalidTxAnchor struct {
	kind   string
	opGeth string // op-geth rejection message substring
	t8n    string // T8n throw message substring (validation_error_contains)
}

var invalidTxAnchorTable = []invalidTxAnchor{
	{"intrinsic_gas", "intrinsic gas too low", "intrinsic gas too low"},
	{"nonce_low", "nonce too low", "nonce too low"},
	{"nonce_high", "nonce too high", "nonce too high"},
	{"insufficient_funds", "insufficient funds for gas * price + value", "insufficient funds for gas * price + value"},
	{"fee_cap_low", "max fee per gas less than block base fee", "max fee per gas less than block base fee"},
	{"sender_no_eoa", "sender not an eoa", "sender not an eoa"},
	// setcode_create: op-geth ErrSetCodeTxCreate is NOT reachable through a real
	// signed SetCodeTx (To() is always non-nil) — the op_geth field is a
	// documentation-only weak anchor; the hard rejection is the T8n opValidate
	// CREATE_SET_CODE_TX path (structured to:null).
	{"setcode_create", "EIP-7702 transaction cannot be used to create contract", "set code transaction must not be a create transaction"},
	{"empty_auth_list", "EIP-7702 transaction with empty auth list", "empty authorization list"},
	// blob: op-geth rejects at block validation ("data blobs present in block
	// body"); FISCO rejects at raw-tx DECODE ("unsupported tx type byte 0x03").
	{"blob", "data blobs present in block body", "unsupported tx type byte 0x03"},
}

func TestInvalidTxCaseSpecsIndependentTable(t *testing.T) {
	// The 9 §4b kinds must live in the INDEPENDENT invalidTxCaseSpecs table and
	// must NOT be reachable from the shared caseSpecs table (review R7:
	// GenerateChainWithGenesis.AddTx rejects invalid txs → emitCases would emit
	// an .in.json that the valid pipeline cannot regenerate).
	if len(invalidTxCaseSpecs) != 9 {
		t.Fatalf("invalidTxCaseSpecs must enumerate 9 kinds, got %d", len(invalidTxCaseSpecs))
	}
	for _, tc := range invalidTxAnchorTable {
		spec, err := invalidTxSpec(tc.kind)
		if err != nil {
			t.Fatalf("invalidTxSpec(%s): %v", tc.kind, err)
		}
		if spec == nil || spec.kind != tc.kind {
			t.Fatalf("invalidTxSpec(%s) returned wrong spec", tc.kind)
		}
		for _, name := range []string{"caseSpecs"} {
			_ = name
		}
	}
	// No invalid-tx kind may be reachable via buildCaseFromSpecs (shared table).
	if _, err := buildCaseFromSpecs("intrinsic_gas", "isthmus"); err == nil {
		t.Fatal("intrinsic_gas must NOT be in the shared caseSpecs table")
	}
}

func TestLegacyTransferEmittedFromEmitCases(t *testing.T) {
	// Task 3 F1 → Task 6: legacy_transfer was a B-scope caseSpec gated OUT of
	// --write-cases; the T8n replayer legacy arm landed (OpT8nReplayTest.cpp),
	// so bScopeSpecs is empty and it must now be emitted (Task 6 B 范围承诺).
	seen := false
	for _, spec := range emitableSpecs() {
		if spec.name == "legacy_transfer" {
			seen = true
		}
	}
	if !seen {
		t.Fatal("legacy_transfer must be emitted by emitCases after the replayer legacy arm landed")
	}
	// And it stays reachable as a base for corrupt/invalid-tx vectors.
	if _, err := buildCaseFromSpecs("legacy_transfer", "isthmus"); err != nil {
		t.Fatalf("legacy_transfer must remain a buildable base caseSpec: %v", err)
	}
}

func TestInvalidIntrinsicGasAnchored(t *testing.T) {
	// deposit 首笔 + gas=0 eip1559（插在 deposit 之后）。
	in, buildInvalid, err := buildInvalidTxCase("intrinsic_gas", "isthmus")
	if err != nil {
		t.Fatalf("buildInvalidTxCase: %v", err)
	}
	cfg, err := buildConfigForCase(&in)
	if err != nil {
		t.Fatalf("buildConfigForCase: %v", err)
	}
	genesis, err := buildGenesisForCase(&in, cfg)
	if err != nil {
		t.Fatalf("buildGenesisForCase: %v", err)
	}
	signer := types.MakeSigner(cfg, big.NewInt(1), uint64(in.Genesis.Timestamp)+10)
	deposit, _, err := buildTx(&in.Transactions[0], signer, cfg)
	if err != nil {
		t.Fatalf("buildTx(deposit): %v", err)
	}
	invalidTx, _, err := buildInvalid(signer, cfg)
	if err != nil {
		t.Fatalf("buildInvalid: %v", err)
	}
	blk, err := buildInvalidTxBlock(&in, cfg, genesis, signer, deposit, invalidTx)
	if err != nil {
		t.Fatalf("buildInvalidTxBlock: %v", err)
	}
	// 手搓 txRoot：types.DeriveSha(types.Transactions(txs), trie.NewStackTrie(nil))。
	txs := []*types.Transaction{deposit, invalidTx}
	if want := types.DeriveSha(types.Transactions(txs), trie.NewStackTrie(nil)); blk.TxHash() != want {
		t.Fatalf("txRoot %s != DeriveSha %s", blk.TxHash().Hex(), want.Hex())
	}
	// header 结构合法：extraData = EncodeOptimismExtraData（否则 VerifyHeaders 先拒）。
	if len(blk.Extra()) == 0 {
		t.Fatal("header extraData empty (must be EncodeOptimismExtraData)")
	}
	// 跑 op-geth ValidateTransaction → 断言含 "intrinsic gas too low"。
	head := blk.Header()
	if err := txpool.ValidateTransaction(invalidTx, head, signer, &txpool.ValidationOptions{
		Config:  cfg,
		Accept:  1 << uint(invalidTx.Type()),
		MaxSize: 128 * 1024,
		MinTip:  big.NewInt(0),
	}); err == nil {
		t.Fatal("ValidateTransaction accepted the gas=0 tx")
	} else if !strings.Contains(err.Error(), "intrinsic gas too low") {
		t.Fatalf("ValidateTransaction want 'intrinsic gas too low', got %q", err.Error())
	}
	// op-geth 块级（InsertChain）也拒绝（iron-rule mirror）。
	if msg, err := captureInsertChainRejection(genesis, blk); err != nil {
		t.Fatalf("captureInsertChainRejection: %v", err)
	} else if !strings.Contains(msg, "intrinsic gas too low") {
		t.Fatalf("InsertChain want 'intrinsic gas too low', got %q", msg)
	}
	// 非法交易插在 deposit 之后（位置 0 以 "first tx is not deposit" 拒，锚错位）。
	if len(blk.Transactions()) != 2 || blk.Transactions()[0].Type() != types.DepositTxType {
		t.Fatalf("block must be [deposit, invalid], got %d txs", len(blk.Transactions()))
	}
}

// buildInvalidTxVectorForTest builds the full vector + block + genesis for a
// kind/fork via the (to-be-implemented) buildInvalidTxVector helper.
func buildInvalidTxVectorForTest(t *testing.T, kind, fork string) (invalidVectorDoc, *types.Block, *core.Genesis) {
	t.Helper()
	doc, blk, genesis, err := buildInvalidTxVector(kind, fork)
	if err != nil {
		t.Fatalf("buildInvalidTxVector(%s, %s): %v", kind, fork, err)
	}
	if blk == nil || genesis == nil {
		t.Fatalf("buildInvalidTxVector(%s, %s): nil blk/genesis", kind, fork)
	}
	return doc, blk, genesis
}

func TestInvalidTxAllKindsRejectSchemaAndAnchors(t *testing.T) {
	for _, tc := range invalidTxAnchorTable {
		for _, fork := range []string{"isthmus", "jovian"} {
			doc, blk, genesis := buildInvalidTxVectorForTest(t, tc.kind, fork)
			rej := rejectOf(doc)
			if rej == nil {
				t.Fatalf("%s/%s: missing reject schema", tc.kind, fork)
			}
			if rej.Fisco.Classification != "INVALID" {
				t.Fatalf("%s/%s: classification want INVALID, got %q", tc.kind, fork, rej.Fisco.Classification)
			}
			if string(rej.Fisco.LatestValidHash) != `"parent"` {
				t.Fatalf("%s/%s: latest_valid_hash want parent, got %s", tc.kind, fork, rej.Fisco.LatestValidHash)
			}
			// consumer 约束（审查 R16）：decode 级 blob → both；其余 → executor。
			wantConsumer := "executor"
			if tc.kind == "blob" {
				wantConsumer = "both"
			}
			if rej.Fisco.Consumer != wantConsumer {
				t.Fatalf("%s/%s: consumer want %q, got %q", tc.kind, fork, wantConsumer, rej.Fisco.Consumer)
			}
			// validation_error_contains 必须携带交易级语义（T8n 断言唯一落点）。
			if rej.Fisco.ValidationErrorContains == "" {
				t.Fatalf("%s/%s: missing validation_error_contains", tc.kind, fork)
			}
			if !strings.Contains(rej.Fisco.ValidationErrorContains, tc.t8n) {
				t.Fatalf("%s/%s: validation_error_contains %q missing t8n anchor %q",
					tc.kind, fork, rej.Fisco.ValidationErrorContains, tc.t8n)
			}
			// 非 decode 级：engine 端不带 validation_error_contains（RTTI-bypass 通用消息）——
			// 即 consumer=executor 时字段只在 executor 端有意义；此处只验证 schema 形状。
			// op_geth 弱锚必须命中期望消息（文档性，但至少非空且语义相关）。
			if rej.OpGeth == "" {
				t.Fatalf("%s/%s: missing op_geth anchor", tc.kind, fork)
			}
			// iron-rule mirror：op-geth InsertChain 必须真实拒绝该块，且消息含期望锚
			// （setcode_create 例外：ErrSetCodeTxCreate 无法经真实信封触达，锚为文档性，
			// InsertChain 的拒绝消息不同——该 kind 的交易级拒绝只由 T8n opValidate 覆盖）。
			if tc.kind == "setcode_create" {
				continue
			}
			msg, err := captureInsertChainRejection(genesis, blk)
			if err != nil {
				t.Fatalf("%s/%s: captureInsertChainRejection: %v", tc.kind, fork, err)
			}
			if !strings.Contains(msg, tc.opGeth) {
				t.Fatalf("%s/%s: InsertChain want op-geth anchor %q, got %q", tc.kind, fork, tc.opGeth, msg)
			}
		}
	}
}

func TestInvalidTxBlobDecodeMessage(t *testing.T) {
	doc, _, _ := buildInvalidTxVectorForTest(t, "blob", "isthmus")
	rej := rejectOf(doc)
	if rej == nil {
		t.Fatal("blob: missing reject")
	}
	// FISCO decode 消息（decode 级 kind）：validation_error_contains 必须是它。
	if !strings.Contains(rej.Fisco.ValidationErrorContains, "unsupported tx type byte 0x03") {
		t.Fatalf("blob: validation_error_contains %q missing decode anchor", rej.Fisco.ValidationErrorContains)
	}
	// 结构化交易对象（block.transactions）必须带 _op_type blob + _op_raw（真实签名 EIP-2718 信封）。
	if len(doc.Block.Transactions) != 2 {
		t.Fatalf("blob: block.transactions must have 2 entries, got %d", len(doc.Block.Transactions))
	}
	var txObj map[string]interface{}
	if err := json.Unmarshal(doc.Block.Transactions[1], &txObj); err != nil {
		t.Fatalf("blob: unmarshal out tx: %v", err)
	}
	if txObj["_op_type"] != "blob" {
		t.Fatalf("blob: _op_type want blob, got %v", txObj["_op_type"])
	}
	if _, ok := txObj["_op_raw"]; !ok {
		t.Fatal("blob: output must carry _op_raw")
	}
	// _op_payload.transactions（engine 消费）携带原始 hex 信封。
	rawTxHex, ok := doc.OpPayload["transactions"].([]string)
	if !ok || len(rawTxHex) != 2 {
		t.Fatalf("blob: _op_payload.transactions must have 2 entries, got %#v", doc.OpPayload["transactions"])
	}
	// 第二笔（blob）必须是 type-0x03 信封（FISCO decode 拒 "unsupported tx type byte 0x03"）。
	if len(rawTxHex[1]) < 4 || rawTxHex[1][:4] != "0x03" {
		t.Fatalf("blob: second envelope must be type 0x03, got %q", rawTxHex[1])
	}
}

func TestInvalidTxManualTxRootAndBlockHash(t *testing.T) {
	// 非法交易参与 txRoot + 占位 stateRoot + blockHash 重算：payload blockHash ==
	// recomputeOpHeaderHash(block.Header())。FISCO 只有过 step-2 blockHash 检查才进执行。
	for _, tc := range invalidTxAnchorTable {
		doc, blk, _ := buildInvalidTxVectorForTest(t, tc.kind, "isthmus")
		if doc.OpPayload["blockHash"] != recomputeOpHeaderHash(blk.Header()).Hex() {
			t.Fatalf("%s: payload blockHash %v != recomputed %v",
				tc.kind, doc.OpPayload["blockHash"], recomputeOpHeaderHash(blk.Header()).Hex())
		}
		// 占位 stateRoot：不必等于真实执行结果（执行前先失败），但必须是 32 字节哈希。
		root, ok := doc.OpPayload["stateRoot"].(string)
		if !ok || !strings.HasPrefix(root, "0x") || len(root) != 66 {
			t.Fatalf("%s: placeholder stateRoot malformed: %v", tc.kind, doc.OpPayload["stateRoot"])
		}
	}
}
