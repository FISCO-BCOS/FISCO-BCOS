// sign_secp — secp256k1 recoverable-signer for the op-e2e suite.
// Usage: sign_secp <privkey_hex> <msg_hash_hex>
// Output (4 lines, consumed by chain_driver.py / b3_contracts.py / probe_l1block.py):
//   line0: <sig raw hex r||s||recid>   (header, ignored by the python parsers)
//   line1: r in hex                    (int(lines[1], 16))
//   line2: s=<hex>                     (split('=')[1])
//   line3: recid=<int>                 (split('=')[1])
// Signs with RFC6979 deterministic nonce, normalizes to low-s (Ethereum requires
// s <= n/2), and reports the recovery id consistent with the low-s signature.
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static int hex_decode(const char* in, unsigned char* out, size_t out_len)
{
    size_t n = strlen(in);
    if (n != out_len * 2)
        return -1;
    for (size_t i = 0; i < out_len; ++i)
    {
        int hi = hex_val(in[2 * i]), lo = hex_val(in[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: sign_secp <privkey_hex> <msg_hash_hex>\n");
        return 2;
    }
    unsigned char seckey[32], msg[32];
    if (hex_decode(argv[1], seckey, 32) != 0 || hex_decode(argv[2], msg, 32) != 0)
    {
        fprintf(stderr, "bad hex input\n");
        return 2;
    }
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx)
    {
        fprintf(stderr, "ctx create failed\n");
        return 1;
    }
    if (secp256k1_ec_seckey_verify(ctx, seckey) != 1)
    {
        fprintf(stderr, "invalid private key\n");
        return 1;
    }
    secp256k1_ecdsa_recoverable_signature sig;
    if (secp256k1_ecdsa_sign_recoverable(ctx, &sig, msg, seckey, NULL, NULL) != 1)
    {
        fprintf(stderr, "sign failed\n");
        return 1;
    }
    // Compact serialization order is [r (32)] [s (32)].
    unsigned char compact[64];
    int recid;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact, &recid, &sig);

    // Low-s normalization: secp256k1's order/2. If s > n/2, s' = n - s and recid flips.
    static const unsigned char half_order[32] = {0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5d, 0x57, 0x6e, 0x73, 0x57, 0xa4, 0x50,
        0x1d, 0xdf, 0xe9, 0x2f, 0x46, 0x68, 0x1b, 0x20, 0xa0};
    int s_high = memcmp(compact + 32, half_order, 32) > 0;
    if (s_high)
    {
        // s' = n - s (field order n =
        // 0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141)
        unsigned char order[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xfe, 0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b, 0xbf,
            0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41};
        unsigned char borrow = 0;
        for (int i = 31; i >= 0; --i)
        {
            unsigned int v = (unsigned int)order[i] - compact[32 + i] - borrow;
            compact[32 + i] = (unsigned char)v;
            borrow = (v >> 8) & 1;
        }
        recid ^= 1;
    }

    char out[256];
    snprintf(out, sizeof(out), "%02x", recid);
    // line0: r||s||recid compact (for debugging / external consumers)
    for (int i = 0; i < 64; ++i)
        snprintf(out + 2 + i * 2, 3, "%02x", compact[i]);
    printf("%s\n", out);  // line0: header
    for (int i = 0; i < 32; ++i)
        printf("%02x", compact[i]);
    printf("\n");  // line1: r
    printf("s=");
    for (int i = 0; i < 32; ++i)
        printf("%02x", compact[32 + i]);
    printf("\n");                           // line2: s=<hex>
    printf("recid=%u\n", (unsigned)recid);  // line3
    secp256k1_context_destroy(ctx);
    return 0;
}
