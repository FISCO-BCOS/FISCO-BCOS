#include "../../src/executive/TransactionExecutive.h"
#include "WasmPath.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasm.h>
#include <wasmtime.h>
#include <boost/test/unit_test.hpp>
#include <chrono>

using namespace std;

int totalGas = 100000000;
int setStorateCalledCount = 0;

namespace bcos::test
{
class GasMeterFixture
{
public:
    GasMeterFixture() { outOfGasTrap = wasmtime_trap_new(outOfGas.c_str(), outOfGas.size()); }
    int count = 10000000;
    int infinitCount = 1000;
    int infinitLoopGas = 10000;
    wasm_trap_t* outOfGasTrap = nullptr;
    std::string outOfGas = "out of gas";
};

BOOST_FIXTURE_TEST_SUITE(testGasMeterPerformance, GasMeterFixture)

static void exit_with_error(const char* message, wasmtime_error_t* error, wasm_trap_t* trap)
{
    fprintf(stderr, "error: %s\n", message);
    wasm_byte_vec_t error_message;
    if (error != NULL)
    {
        wasmtime_error_message(error, &error_message);
    }
    else
    {
        wasm_trap_message(trap, &error_message);
    }
    fprintf(stderr, "%.*s\n", (int)error_message.size, error_message.data);
    wasm_byte_vec_delete(&error_message);
    exit(1);
}

// A function to be called from Wasm code.
wasm_trap_t* bcos_setStorage(void* env, const wasm_val_vec_t* args, wasm_val_vec_t* results)
{
    std::ignore = env;
    std::ignore = results;
    printf("bcos_setStorage...%d\n> ", args->data[0].of.i32);
    printf("\n");
    return NULL;
}

wasm_trap_t* bcos_setStorage_wasmtime(void* env, wasmtime_caller_t* caller,
    const wasmtime_val_t* args, size_t nargs, wasmtime_val_t* results, size_t nresults)
{
    ++setStorateCalledCount;
    return NULL;
}

wasm_trap_t* bcos_useGas_wasmtime(void* env, wasmtime_caller_t* caller, const wasmtime_val_t* args,
    size_t nargs, wasmtime_val_t* results, size_t nresults)
{
    static int64_t all = totalGas;
    auto testFixture = (GasMeterFixture*)env;
    all -= args[0].of.i64;
    if (all <= 0)
    {
        cout << "bcos_useGas outOfGas" << endl;
        return testFixture->outOfGasTrap;
    }
    return NULL;
}

wasm_trap_t* bcos_useGas_infinit_wasmtime(void* env, wasmtime_caller_t* caller,
    const wasmtime_val_t* args, size_t nargs, wasmtime_val_t* results, size_t nresults)
{
    auto testFixture = (GasMeterFixture*)env;
    static int64_t all = testFixture->infinitLoopGas;
    all -= args[0].of.i64;
    if (all <= 0)
    {
        cout << "bcos_useGas outOfGas" << endl;
        return testFixture->outOfGasTrap;
    }
    return NULL;
}

wasm_trap_t* bcos_outOfGas_wasmtime(void* env, wasmtime_caller_t* caller,
    const wasmtime_val_t* args, size_t nargs, wasmtime_val_t* results, size_t nresults)
{
    // interput execution
    cout << "bcos_outOfGas called" << endl;
    auto testFixture = (GasMeterFixture*)env;
    return testFixture->outOfGasTrap;
}

wasm_functype_t* wasm_functype_new_4_0(
    wasm_valtype_t* p1, wasm_valtype_t* p2, wasm_valtype_t* p3, wasm_valtype_t* p4)
{
    wasm_valtype_t* ps[4] = {p1, p2, p3, p4};
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new(&params, 4, ps);
    wasm_valtype_vec_new_empty(&results);
    return wasm_functype_new(&params, &results);
}

BOOST_AUTO_TEST_CASE(origin_wasmc)
{
    printf("Initializing...\n");
    wasm_engine_t* engine = wasm_engine_new();
    wasm_store_t* store = wasm_store_new(engine);

    // Load binary.
    printf("Loading binary...\n");
    FILE* file = fopen(originBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading module!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t binary;
    wasm_byte_vec_new_uninitialized(&binary, file_size);
    if (fread(binary.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile.
    printf("Compiling module...\n");
    wasm_module_t* module = wasm_module_new(store, &binary);
    if (!module)
    {
        BOOST_FAIL("> Error compiling module!\n");
        return;
    }

    wasm_byte_vec_delete(&binary);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasm_func_t* setStorage_func =
        wasm_func_new_with_env(store, bcos_setStorage_type, bcos_setStorage, nullptr, nullptr);

    wasm_functype_delete(bcos_setStorage_type);

    // Instantiate.
    printf("Instantiating module...\n");
    wasm_extern_t* externs[] = {wasm_func_as_extern(setStorage_func)};
    wasm_extern_vec_t imports = WASM_ARRAY_VEC(externs);
    wasm_instance_t* instance = wasm_instance_new(store, module, &imports, NULL);
    if (!instance)
    {
        BOOST_FAIL("> Error instantiating module!\n");
        return;
    }

    wasm_func_delete(setStorage_func);

    // Extract export.
    printf("Extracting export...\n");
    wasm_extern_vec_t exports;
    wasm_instance_exports(instance, &exports);
    if (exports.size == 0)
    {
        BOOST_FAIL("> Error accessing exports!\n");
        return;
    }
    const wasm_func_t* run_func = wasm_extern_as_func(exports.data[1]);
    if (run_func == NULL)
    {
        BOOST_FAIL("> Error accessing export!\n");
        return;
    }

    wasm_module_delete(module);
    wasm_instance_delete(instance);

    // Call.
    printf("Calling export %d times...\n", count);
    wasm_val_vec_t args{0, nullptr};
    wasm_val_vec_t results{0, nullptr};
    auto start = chrono::system_clock::now();
    for (int n = 0; n < count; n++)
    {
        if (wasm_func_call(run_func, &args, &results))
        {
            printf("> Error calling function!\n");
            return;
        }
        // printf("Printing result...\n");
        // printf("> %u\n", rs[0].of.i32);
    }
    auto end = chrono::system_clock::now();
    cout << "origin_wasmc call function " << count << " times, time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count() << endl;
    wasm_extern_vec_delete(&exports);

    // printf("Shutting down...\n");
    wasm_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_CASE(origin_wasmtime)
{
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);

    // Load our input file to parse it next
    FILE* file = fopen(originBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);
    wasmtime_extern_t import;
    import.kind = WASMTIME_EXTERN_FUNC;
    import.of.func = setStorage_func;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, &import, 1, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "main", strlen("main"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < count; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL || trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel computing main(%d)\n", n);
            break;
        }
    }
    auto end = chrono::system_clock::now();
    cout << "origin_wasmtime call function " << count << " times, time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count() << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_CASE(origin_function_call_wasmtime)
{
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);

    // Load our input file to parse it next
    FILE* file = fopen(originBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);
    wasmtime_extern_t import;
    import.kind = WASMTIME_EXTERN_FUNC;
    import.of.func = setStorage_func;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, &import, 1, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "main2", strlen("main2"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < count; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL || trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel computing main(%d)\n", n);
            break;
        }
    }
    auto end = chrono::system_clock::now();
    cout << "origin_wasmtime function call function " << count << " times, time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count() << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_CASE(origin_fuel)
{
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    wasmtime_config_consume_fuel_set(config, true);

    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);

    error = wasmtime_context_add_fuel(context, totalGas);
    if (error != NULL)
        exit_with_error("failed to add fuel", error, NULL);

    // Load our input file to parse it next
    FILE* file = fopen(originBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);
    wasmtime_extern_t import;
    import.kind = WASMTIME_EXTERN_FUNC;
    import.of.func = setStorage_func;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, &import, 1, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    // Lookup our `fibonacci` export function
    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "main", strlen("main"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // uint64_t fuel_before;
    // wasmtime_context_fuel_consumed(context, &fuel_before);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < count; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL || trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel computing main(%d)\n", n);
            break;
        }
    }
    // uint64_t fuel_after;
    // wasmtime_context_fuel_consumed(context, &fuel_after);

    // error = wasmtime_context_add_fuel(context, fuel_after - fuel_before);
    // if (error != NULL)
    //     exit_with_error("failed to add fuel", error, NULL);
    // printf(
    //     "main(%d) [consumed %lld fuel]\n", n, fuel_after - fuel_before);
    auto end = chrono::system_clock::now();
    cout << "origin_fuel call function " << count << " times, time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count() << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_CASE(useGas)
{
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);

    // Load our input file to parse it next
    FILE* file = fopen(useGasBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);

    wasm_functype_t* bcos_useGas_type = wasm_functype_new_1_0(wasm_valtype_new_i64());
    wasmtime_func_t useGas_func;
    wasmtime_func_new(context, bcos_useGas_type, bcos_useGas_wasmtime, this, NULL, &useGas_func);

    wasmtime_extern_t imports[2];
    imports[0].kind = WASMTIME_EXTERN_FUNC;
    imports[0].of.func = setStorage_func;
    imports[1].kind = WASMTIME_EXTERN_FUNC;
    imports[1].of.func = useGas_func;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, imports, 2, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "main", strlen("main"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < count; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL || trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel computing main(%d)\n", n);
            break;
        }
    }
    auto end = chrono::system_clock::now();
    cout << "useGas call function " << count << " times, time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count() << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_CASE(globalGas)
{
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);

    // Load our input file to parse it next
    FILE* file = fopen(globalGasBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);

    wasm_functype_t* bcos_outOfGas_type = wasm_functype_new_0_0();
    wasmtime_func_t outOfGas_func;
    wasmtime_func_new(
        context, bcos_outOfGas_type, bcos_outOfGas_wasmtime, this, NULL, &outOfGas_func);

    wasmtime_global_t globalGas;
    wasm_mutability_t mut = wasm_mutability_enum::WASM_VAR;
    wasm_globaltype_t* globalGas_type = wasm_globaltype_new(wasm_valtype_new_i64(), mut);
    wasmtime_val_t initValue;
    initValue.kind = WASMTIME_I64;
    initValue.of.i64 = totalGas;
    wasmtime_global_new(context, globalGas_type, &initValue, &globalGas);

    wasmtime_extern_t imports[3];
    imports[0].kind = WASMTIME_EXTERN_FUNC;
    imports[0].of.func = setStorage_func;
    imports[1].kind = WASMTIME_EXTERN_FUNC;
    imports[1].of.func = outOfGas_func;
    imports[2].kind = WASMTIME_EXTERN_GLOBAL;
    imports[2].of.global = globalGas;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, imports, 3, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "main", strlen("main"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < count; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL || trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel computing main(%d)\n", n);
            break;
        }
    }
    auto end = chrono::system_clock::now();
    wasmtime_val_t finalValue;
    wasmtime_global_get(context, &globalGas, &finalValue);
    BOOST_TEST(finalValue.kind, WASMTIME_I64);
    cout << "globalGas call function " << count << " times, time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count()
         << ", left=" << finalValue.of.i64 << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_CASE(globalGas_infinit_loop)
{
    setStorateCalledCount = 0;
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);

    // Load our input file to parse it next
    FILE* file = fopen(globalGasBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);

    wasm_functype_t* bcos_outOfGas_type = wasm_functype_new_0_0();
    wasmtime_func_t outOfGas_func;
    wasmtime_func_new(
        context, bcos_outOfGas_type, bcos_outOfGas_wasmtime, this, NULL, &outOfGas_func);

    wasmtime_global_t globalGas;
    wasm_mutability_t mut = wasm_mutability_enum::WASM_VAR;
    wasm_globaltype_t* globalGas_type = wasm_globaltype_new(wasm_valtype_new_i64(), mut);
    wasmtime_val_t initValue;
    initValue.kind = WASMTIME_I64;
    initValue.of.i64 = infinitLoopGas;
    wasmtime_global_new(context, globalGas_type, &initValue, &globalGas);

    wasmtime_extern_t imports[3];
    imports[0].kind = WASMTIME_EXTERN_FUNC;
    imports[0].of.func = setStorage_func;
    imports[1].kind = WASMTIME_EXTERN_FUNC;
    imports[1].of.func = outOfGas_func;
    imports[2].kind = WASMTIME_EXTERN_GLOBAL;
    imports[2].of.global = globalGas;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, imports, 3, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "deploy", strlen("deploy"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < infinitCount; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel infinite loop(%d)\n", n);
            break;
        }
        if (trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasm_trap_message(trap, &error_message);
            wasm_trap_delete(trap);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel infinite loop(%d)\n", n);
            break;
        }
    }
    auto end = chrono::system_clock::now();
    wasmtime_val_t finalValue;
    wasmtime_global_get(context, &globalGas, &finalValue);
    BOOST_TEST(finalValue.kind, WASMTIME_I64);
    cout << "globalGas call infinite loop function,time used(us)="
         << chrono::duration_cast<chrono::microseconds>(end - start).count()
         << ", left=" << finalValue.of.i64 << ", setStorage called = " << setStorateCalledCount
         << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}


BOOST_AUTO_TEST_CASE(useGas_infinit_loop)
{
    totalGas = infinitLoopGas;
    setStorateCalledCount = 0;
    wasmtime_error_t* error = NULL;

    wasm_config_t* config = wasm_config_new();
    assert(config != NULL);
    // Create an *engine*, which is a compilation context, with our configured options.
    wasm_engine_t* engine = wasm_engine_new_with_config(config);
    assert(engine != NULL);
    wasmtime_store_t* store = wasmtime_store_new(engine, NULL, NULL);
    assert(store != NULL);
    wasmtime_context_t* context = wasmtime_store_context(store);
    // Load our input file to parse it next
    FILE* file = fopen(useGasBinary, "r");
    if (!file)
    {
        BOOST_FAIL("> Error reading file!\n");
        return;
    }
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    wasm_byte_vec_t wasm;
    wasm_byte_vec_new_uninitialized(&wasm, file_size);
    if (fread(wasm.data, file_size, 1, file) != 1)
    {
        BOOST_FAIL("> Error loading module!\n");
        return;
    }
    fclose(file);

    // Compile and instantiate our module
    wasmtime_module_t* module = NULL;
    error = wasmtime_module_new(engine, (uint8_t*)wasm.data, wasm.size, &module);
    if (module == NULL)
        exit_with_error("failed to compile module", error, NULL);
    wasm_byte_vec_delete(&wasm);

    // Create external print functions.
    wasm_functype_t* bcos_setStorage_type = wasm_functype_new_4_0(wasm_valtype_new_i32(),
        wasm_valtype_new_i32(), wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasmtime_func_t setStorage_func;
    wasmtime_func_new(
        context, bcos_setStorage_type, bcos_setStorage_wasmtime, NULL, NULL, &setStorage_func);
    // wasm_functype_delete(bcos_setStorage_type);

    wasm_functype_t* bcos_useGas_type = wasm_functype_new_1_0(wasm_valtype_new_i64());
    wasmtime_func_t useGas_func;
    wasmtime_func_new(
        context, bcos_useGas_type, bcos_useGas_infinit_wasmtime, this, NULL, &useGas_func);

    wasmtime_extern_t imports[2];
    imports[0].kind = WASMTIME_EXTERN_FUNC;
    imports[0].of.func = setStorage_func;
    imports[1].kind = WASMTIME_EXTERN_FUNC;
    imports[1].of.func = useGas_func;

    wasm_trap_t* trap = NULL;
    wasmtime_instance_t instance;
    error = wasmtime_instance_new(context, module, imports, 2, &instance, &trap);
    if (error != NULL || trap != NULL)
        exit_with_error("failed to instantiate", error, trap);

    wasmtime_extern_t main;
    bool ok = wasmtime_instance_export_get(context, &instance, "deploy", strlen("deploy"), &main);
    assert(ok);
    assert(main.kind == WASMTIME_EXTERN_FUNC);

    // Call it repeatedly until it fails
    auto start = chrono::system_clock::now();
    for (int n = 0; n < infinitCount; n++)
    {
        error = wasmtime_func_call(context, &main.of.func, nullptr, 0, nullptr, 0, &trap);
        if (error != NULL)
        {
            wasm_byte_vec_t error_message;
            wasmtime_error_message(error, &error_message);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel infinite loop(%d)\n", n);
            break;
        }
        if (trap != NULL)
        {
            wasm_byte_vec_t error_message;
            wasm_trap_message(trap, &error_message);
            wasm_trap_delete(trap);
            cout << string(error_message.data, error_message.size) << endl;
            printf("Exhausted fuel infinite loop(%d)\n", n);
            break;
        }
    }
    auto end = chrono::system_clock::now();
    cout << "useGas call infinite loop function"
         << ", time used(us)=" << chrono::duration_cast<chrono::microseconds>(end - start).count()
         << ", setStorage called = " << setStorateCalledCount << endl;

    // Clean up after ourselves at this point
    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test