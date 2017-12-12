#include <stdio.h>
#include <evmjit.h>

int main()
{
    struct evm_interface evmjit = evmjit_get_interface();
    static const unsigned expected_abi_version = 0;
    printf("EVMJIT ABI version %u\n", evmjit.abi_version);
    if (expected_abi_version != evmjit.abi_version)
    {
        printf("Error: expected ABI version %u!\n", expected_abi_version);
        return 1;
    }
    return 0;
}
