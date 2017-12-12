#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "evm.h"


struct evm_uint256be balance(struct evm_env* env, struct evm_uint160be address)
{
    struct evm_uint256be ret = {.bytes = {1, 2, 3, 4}};
    return ret;
}

struct evm_uint160be address(struct evm_env* env)
{
    struct evm_uint160be ret = {.bytes = {1, 2, 3, 4}};
    return ret;
}

union evm_variant query(struct evm_env* env,
                        enum evm_query_key key,
                        union evm_variant arg) {
    union evm_variant result;
    printf("EVM-C: QUERY %d\n", key);
    switch (key) {
    case EVM_GAS_LIMIT: result.int64 = 314; break;

    case EVM_BALANCE:
        result.uint256be = balance(env, arg.address);
        break;

    case EVM_ADDRESS:
        result.address = address(env);
        break;

    default: result.int64 = 0; break;
    }
    return result;
}

void update(struct evm_env* env,
            enum evm_update_key key,
            union evm_variant arg1,
            union evm_variant arg2)
{
    printf("EVM-C: UPDATE %d\n", key);
}

int64_t call(
    struct evm_env* _opaqueEnv,
    enum evm_call_kind _kind,
    int64_t _gas,
    struct evm_uint160be _address,
    struct evm_uint256be _value,
    uint8_t const* _inputData,
    size_t _inputSize,
    uint8_t* _outputData,
    size_t _outputSize
)
{
    printf("EVM-C: CALL %d\n", _kind);
    return EVM_CALL_FAILURE;
}

/// Example how the API is supposed to be used.
int main(int argc, char *argv[]) {
    struct evm_interface intf = examplevm_get_interface();
    struct evm_instance* jit = intf.create(query, update, call);

    char const code[] = "exec()";
    const size_t code_size = sizeof(code);
    struct evm_uint256be code_hash = {.bytes = {1, 2, 3,}};
    char const input[] = "Hello World!";
    struct evm_uint256be value = {{1, 0, 0, 0}};

    int64_t gas = 200000;
    struct evm_result result =
        intf.execute(jit, NULL, EVM_HOMESTEAD, code_hash, (const uint8_t *)code, code_size, gas, (const uint8_t *)input,
                    sizeof(input), value);

    printf("Execution result:\n");
    if (result.code != EVM_SUCCESS) {
      printf("  EVM execution failure: %d\n", result.code);
    } else {
        printf("  Gas used: %ld\n", gas - result.gas_left);
        printf("  Gas left: %ld\n", result.gas_left);
        printf("  Output size: %zd\n", result.output_size);

        printf("  Output: ");
        size_t i = 0;
        for (i = 0; i < result.output_size; i++) {
            printf("%02x ", result.output_data[i]);
        }
        printf("\n");
    }

    result.release(&result);
    intf.destroy(jit);
}
