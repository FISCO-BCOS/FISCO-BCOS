#include "kzgPrecompiled.h"
#include <blst.h>

using G1 = blst_p1;
using G2 = blst_p2;
using Fr = blst_fr;

constexpr static const G2 kKzgSetupG2_1{
    {{{0x6120a2099b0379f9, 0xa2df815cb8210e4e, 0xcb57be5577bd3d4f, 0x62da0ea89a0c93f8,
          0x02e0ee16968e150d, 0x171f09aea833acd5},
        {0x11a3670749dfd455, 0x04991d7b3abffadc, 0x85446a8e14437f41, 0x27174e7b4e76e3f2,
            0x7bfa6dd397f60a20, 0x02fcc329ac07080f}}},
    {{{0xaa130838793b2317, 0xe236dd220f891637, 0x6502782925760980, 0xd05c25f60557ec89,
          0x6095767a44064474, 0x185693917080d405},
        {0x549f9e175b03dc0a, 0x32c0c95a77106cfe, 0x64a74eae5705d080, 0x53deeaf56659ed9e,
            0x09a1d368508afb93, 0x12cf3a4525b5e9bd}}},
    {{{0x760900000002fffd, 0xebf4000bc40c0002, 0x5f48985753c758ba, 0x77ce585370525745,
          0x5c071a97a256ec6d, 0x15f65ec3fa80e493},
        {0x0000000000000000, 0x0000000000000000, 0x0000000000000000, 0x0000000000000000,
            0x0000000000000000, 0x0000000000000000}}}};

static bool pairings_verify(const G1* a1, const G2* a2, const G1* b1, const G2* b2)
{
    blst_fp12 loop0, loop1, gt_point;
    blst_p1_affine aa1, bb1;
    blst_p2_affine aa2, bb2;

    G1 a1neg = *a1;
    blst_p1_cneg(&a1neg, true);

    blst_p1_to_affine(&aa1, &a1neg);
    blst_p1_to_affine(&bb1, b1);
    blst_p2_to_affine(&aa2, a2);
    blst_p2_to_affine(&bb2, b2);

    blst_miller_loop(&loop0, &aa2, &aa1);
    blst_miller_loop(&loop1, &bb2, &bb1);

    blst_fp12_mul(&gt_point, &loop0, &loop1);
    blst_final_exp(&gt_point, &gt_point);

    return blst_fp12_is_one(&gt_point);
}

static void g1_mul(G1* out, const G1* a, const Fr* b)
{
    blst_scalar s;
    blst_scalar_from_fr(&s, b);
    blst_p1_mult(out, a, s.b, 8 * sizeof(blst_scalar));
}

static void g2_mul(G2* out, const G2* a, const Fr* b)
{
    blst_scalar s;
    blst_scalar_from_fr(&s, b);
    blst_p2_mult(out, a, s.b, 8 * sizeof(blst_scalar));
}

static void g1_sub(G1* out, const G1* a, const G1* b)
{
    G1 bneg = *b;
    blst_p1_cneg(&bneg, true);
    blst_p1_add_or_double(out, a, &bneg);
}

static void g2_sub(G2* out, const G2* a, const G2* b)
{
    G2 bneg = *b;
    blst_p2_cneg(&bneg, true);
    blst_p2_add_or_double(out, a, &bneg);
}

static bool validate_kzg_g1(G1* out, bcos::bytesConstRef b)
{
    blst_p1_affine p1_affine;

    /* Convert the bytes to a p1 point */
    /* The uncompress routine checks that the point is on the curve */
    if (blst_p1_uncompress(&p1_affine, b.data()) != BLST_SUCCESS)
    {
        return false;
    }
    blst_p1_from_affine(out, &p1_affine);

    /* The point at infinity is accepted! */
    if (blst_p1_is_inf(out))
    {
        return true;
    }

    /* The point must be on the right subgroup */
    return blst_p1_in_g1(out);
}

static bool bytes_to_bls_field(Fr* out, bcos::bytesConstRef b)
{
    blst_scalar tmp;
    blst_scalar_from_bendian(&tmp, b.data());
    if (!blst_scalar_fr_check(&tmp))
    {
        return false;
    }
    blst_fr_from_scalar(out, &tmp);
    return true;
}

bcos::crypto::HashType bcos::executor::crypto::kzgPrecompiled::kzg2VersionedHash(
    bytesConstRef input)
{
    auto sha256 = std::make_shared<bcos::crypto::Sha256>();
    auto hash = sha256->hash(input);
    BCOS_LOG(DEBUG) << LOG_DESC("hash") << LOG_KV("hash", hash) << LOG_KV("hash hex", hash.hex());
    hash[0] = kBlobCommitmentVersionKzg;
    BCOS_LOG(DEBUG) << LOG_DESC("a") << LOG_KV("a", hash) << LOG_KV("a hex", hash.hex());
    return hash;
}

static bool verifyKZGProofImpl(const G1* commitment, const Fr* z, const Fr* y, const G1* proof)
{
    G2 x_g2, X_minus_z;
    G1 y_g1, P_minus_y;

    /* Calculate: X_minus_z */
    g2_mul(&x_g2, blst_p2_generator(), z);
    g2_sub(&X_minus_z, &kKzgSetupG2_1, &x_g2);

    /* Calculate: P_minus_y */
    g1_mul(&y_g1, blst_p1_generator(), y);
    g1_sub(&P_minus_y, commitment, &y_g1);

    /* Verify: P - y = Q * (X - z) */
    return pairings_verify(&P_minus_y, blst_p2_generator(), proof, &X_minus_z);
}

bool bcos::executor::crypto::kzgPrecompiled::verifyKZGProof(
    bytesConstRef commitment, bytesConstRef z, bytesConstRef y, bytesConstRef proof)
{
    Fr z_fr, y_fr;
    G1 commitment_g1, proof_g1;

    if (!validate_kzg_g1(&commitment_g1, commitment))
    {
        return false;
    }
    if (!bytes_to_bls_field(&z_fr, z))
    {
        return false;
    }
    if (!bytes_to_bls_field(&y_fr, y))
    {
        return false;
    }
    if (!validate_kzg_g1(&proof_g1, proof))
    {
        return false;
    }

    return verifyKZGProofImpl(&commitment_g1, &z_fr, &y_fr, &proof_g1);
}
