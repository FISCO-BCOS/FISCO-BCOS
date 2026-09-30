#define _GNU_SOURCE /* For fmemopen */

#include "ckzg_embed.h"

#include <stdio.h>

extern const char ckzg_embedded_trusted_setup[];
extern const unsigned long ckzg_embedded_trusted_setup_size;

C_KZG_RET load_trusted_setup_embedded(KZGSettings *out, uint64_t precompute)
{
    FILE *fp = fmemopen((void *)ckzg_embedded_trusted_setup,
        (size_t)ckzg_embedded_trusted_setup_size, "r");
    if (fp == NULL)
    {
        return C_KZG_MALLOC;
    }
    C_KZG_RET ret = load_trusted_setup_file(out, fp, precompute);
    fclose(fp);
    return ret;
}
