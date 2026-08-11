package main

// Task 3 TDD tests. ⚠️ Every test references at least one function that is
// UNDEFINED before the implementation lands (buildBlockVector / corruptVector /
// captureInsertChainRejection / recomputeOpHeaderHash / buildCaseFromSpecs /
// emitStaticSurfaceVector / staticSurfaceItems / legacy arm). Before Step 3-6
// this file fails to COMPILE (real red); after implementation it goes green.

import (
	"encoding/json"
	"strings"
	"testing"
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
