// Valid-input construction helpers for the Phase 2 line-B precompile vectors
// (Task 0). Each helper builds an input that the corresponding op-geth
// precompile ACCEPTS (parseable + on-curve/subgroup where the precompile
// checks, or a proof/signature that actually verifies). Encoding sizes are the
// authoritative EIP/RIP forms, NOT the bn256 sizes:
//
//	bn256 (alt_bn128, EIP-196/197): G1 = 64B (x||y, 2x32), G2 = 128B,
//	                               pair = 192B.
//	BLS12-381 (EIP-2537): UNCOMPRESSED: Fp = 64B (16 zero bytes || 48B
//	                               big-endian; 381-bit field), G1 = 128B
//	                               (x,y in Fp), G2 = 256B (x,y in Fp2, each
//	                               c0||c1 in Fp). Mirrors core/vm
//	                               decodeBLS12381FieldElement/encodePointG1/G2.
//	secp256r1 (RIP-7212): 160B = hash(32) || r(32) || s(32) || x(32) || y(32)
//	                               -- the ORDER in op-geth core/vm p256Verify
//	                               .Run (input[0:32] hash, [32:64] r, [64:96] s,
//	                               [96:128] x, [128:160] y).
//	KZG (EIP-4844): 192B = versionedHash(32) || z(32) || y(32) ||
//	                       commitment(48) || proof(48).
//
// Verification (the `// go test`-style assertions) is performed by the
// --probe-precompile dev probe in main.go: every helper output is fed to the
// REAL op-geth precompile Run (core/vm.PrecompiledContractsOsaka) and must
// return no error (plus, for p256/KZG, the exact expected output). All helpers
// are deterministic.
package main

import (
	"crypto/sha256"
	"math/big"

	bls12381 "github.com/consensys/gnark-crypto/ecc/bls12-381"
	"github.com/ethereum/go-ethereum/common/hexutil"
	bn256cf "github.com/ethereum/go-ethereum/crypto/bn256/cloudflare"
	"github.com/ethereum/go-ethereum/crypto/kzg4844"
)

// repeatedPair repeats a byte slice count times (the generic building block for
// multi-pair / multi-point precompile inputs).
func repeatedPair(pair []byte, count int) []byte {
	if count < 0 {
		panic("repeatedPair: negative count")
	}
	out := make([]byte, 0, len(pair)*count)
	for i := 0; i < count; i++ {
		out = append(out, pair...)
	}
	return out
}

// validBn256G1 returns one valid alt_bn128 G1 point (64 bytes = x||y, each 32B
// big-endian). Constructed as [1]G1 (the generator point) via the cloudflare
// bn256 backend; the EVM format for G1 is identical across cloudflare/gnark, so
// the marshal output is directly what the precompile's G1.Unmarshal accepts.
func validBn256G1() []byte {
	g1 := new(bn256cf.G1).ScalarBaseMult(big.NewInt(1))
	out := g1.Marshal()
	if len(out) != 64 {
		panic("validBn256G1: unexpected marshal length")
	}
	return out
}

// validBn256G2 returns one valid alt_bn128 G2 point (128 bytes). The cloudflare
// backend's Marshal order (x.x||x.y||y.x||y.y) is byte-identical to what the
// top-level crypto/bn256 package's G2.Unmarshal (gnark on amd64/arm64, google
// elsewhere) expects -- verified empirically by --probe-precompile, which
// brute-forced all 24 permutations of the four 32B words against gnark and
// found [0,1,2,3] (the raw marshal) is the accepted one. No re-ordering needed.
func validBn256G2() []byte {
	g2 := new(bn256cf.G2).ScalarBaseMult(big.NewInt(1))
	cf := g2.Marshal()
	if len(cf) != 128 {
		panic("validBn256G2: unexpected marshal length")
	}
	return cf
}

// repeatedBn256Pair returns k copies of a valid bn256 pairing input
// (192 bytes = G1 64 + G2 128, per EIP-197).
func repeatedBn256Pair(k int) []byte {
	pair := append(validBn256G1(), validBn256G2()...)
	if len(pair) != 192 {
		panic("repeatedBn256Pair: pair length mismatch")
	}
	return repeatedPair(pair, k)
}

// blsFpBytes returns the 48-byte big-endian encoding of a BLS12-381 field
// element (the low bytes of the EIP-2537 64-byte Fp slot).
func blsFpBytes(b [48]byte) []byte {
	return b[:]
}

// validBlsG1 returns one valid BLS12-381 G1 point in the EIP-2537 UNCOMPRESSED
// encoding: 128 bytes = x(64B) || y(64B), each Fp slot = 16 zero bytes || 48B
// big-endian field element. Constructed as [1]g1 (the generator) via
// gnark-crypto (the same bls12381 package core/vm precompiles use) and encoded
// exactly like core/vm encodePointG1.
func validBlsG1() []byte {
	var p bls12381.G1Affine
	p.ScalarMultiplicationBase(big.NewInt(1))
	if !p.IsOnCurve() || !p.IsInSubGroup() {
		panic("validBlsG1: generator not on curve/subgroup")
	}
	out := make([]byte, 128)
	copy(out[16:64], blsFpBytes(p.X.Bytes()))
	copy(out[80:128], blsFpBytes(p.Y.Bytes()))
	return out
}

// validBlsG2 returns one valid BLS12-381 G2 point in the EIP-2537 UNCOMPRESSED
// encoding: 256 bytes = x(128B) || y(128B), each Fp2 = c0(64B) || c1(64B), each
// Fp slot = 16 zero bytes || 48B big-endian. Encoded exactly like core/vm
// encodePointG2 (x.A0, x.A1, y.A0, y.A1).
func validBlsG2() []byte {
	var p bls12381.G2Affine
	p.ScalarMultiplicationBase(big.NewInt(1))
	if !p.IsOnCurve() || !p.IsInSubGroup() {
		panic("validBlsG2: generator not on curve/subgroup")
	}
	out := make([]byte, 256)
	copy(out[16:64], blsFpBytes(p.X.A0.Bytes()))
	copy(out[80:128], blsFpBytes(p.X.A1.Bytes()))
	copy(out[144:192], blsFpBytes(p.Y.A0.Bytes()))
	copy(out[208:256], blsFpBytes(p.Y.A1.Bytes()))
	return out
}

// validP256Sig returns a valid RIP-7212 secp256r1 verification input
// (160 bytes = hash || r || s || x || y, matching op-geth p256Verify.Run's
// slice order). Inlined as a hex constant generated once (see below): private
// key scalar 0x01 (public key = the secp256r1 generator), message
// sha256("fisco line-b p256 vector"), produced and stdlib-verified by a
// one-off program; re-verified against op-geth crypto/secp256r1.Verify (via
// the p256Verify precompile) by --probe-precompile.
func validP256Sig() []byte {
	return hexutil.MustDecode("0x" +
		// hash = sha256("fisco line-b p256 vector")
		"83571dbb808737a2c0e9073de851ede5617b614ec79efea8a56032258f66338a" +
		// r
		"1ae7651cef64eafa8d8637c9d921802c7a7f34239271ae11c8a2977d548cfa4d" +
		// s
		"6aa2d187b2280a566e3d21827ea5f997b95aaa29ee114b6e8af4b9b725114f3f" +
		// x (generator x = 6b17d1f2…)
		"6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296" +
		// y (generator y = 4fe342e2…)
		"4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5")
}

// validKZGInput returns a valid EIP-4844 point-evaluation input
// (192 bytes = versionedHash(32) || z(32) || y(32) || commitment(48) ||
// proof(48)). A fixed 131072-byte blob (every byte 0x01, so every 32-byte
// field element is < the BLS scalar modulus) is committed, a proof at a fixed
// point z = 0x0001…1f is computed, and the versioned hash is sha256(commitment)
// with the EIP-4844 version byte 0x01. kzg4844.VerifyProof (the precompile's
// own check) then succeeds by construction.
func validKZGInput() []byte {
	var blob kzg4844.Blob
	for i := range blob {
		blob[i] = 0x01
	}
	commitment, err := kzg4844.BlobToCommitment(&blob)
	if err != nil {
		panic("validKZGInput: BlobToCommitment: " + err.Error())
	}
	var point kzg4844.Point
	for i := range point {
		point[i] = byte(i)
	}
	proof, claim, err := kzg4844.ComputeProof(&blob, point)
	if err != nil {
		panic("validKZGInput: ComputeProof: " + err.Error())
	}
	versionedHash := sha256.Sum256(commitment[:])
	versionedHash[0] = 0x01 // EIP-4844 commitment version (KZG)
	in := make([]byte, 192)
	copy(in[0:32], versionedHash[:])
	copy(in[32:64], point[:])
	copy(in[64:96], claim[:])
	copy(in[96:144], commitment[:])
	copy(in[144:192], proof[:])
	return in
}
