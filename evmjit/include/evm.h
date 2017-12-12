/// EVM-C -- C interface to Ethereum Virtual Machine
///
/// ## High level design rules
/// 1. Pass function arguments and results by value.
///    This rule comes from modern C++ and tries to avoid costly alias analysis
///    needed for optimization. As the result we have a lots of complex structs
///    and unions. And variable sized arrays of bytes cannot be passed by copy.
/// 2. The EVM operates on integers so it prefers values to be host-endian.
///    On the other hand, LLVM can generate good code for byte swaping.
///    The interface also tries to match host application "natural" endianess.
///    I would like to know what endianess you use and where.
///
/// @defgroup EVMC EVM-C
/// @{
#ifndef EVM_H
#define EVM_H

#include <stdint.h>    // Definition of int64_t, uint64_t.
#include <stddef.h>    // Definition of size_t.

#if __cplusplus
extern "C" {
#endif

// BEGIN Python CFFI declarations

/// The EVM-C ABI version number matching the interface declared in this file.
static const uint32_t EVM_ABI_VERSION = 0;

/// Big-endian 256-bit integer.
///
/// 32 bytes of data representing big-endian 256-bit integer. I.e. bytes[0] is
/// the most significant byte, bytes[31] is the least significant byte.
/// This type is used to transfer to/from the VM values interpreted by the user
/// as both 256-bit integers and 256-bit hashes.
struct evm_uint256be {
    /// The 32 bytes of the big-endian integer or hash.
    uint8_t bytes[32];
};

/// Big-endian 160-bit hash suitable for keeping an Ethereum address.
struct evm_uint160be {
    /// The 20 bytes of the hash.
    uint8_t bytes[20];
};

/// The execution result code.
enum evm_result_code {
    EVM_SUCCESS = 0,               ///< Execution finished with success.
    EVM_FAILURE = 1,               ///< Generic execution failure.
    EVM_OUT_OF_GAS = 2,
    EVM_BAD_INSTRUCTION = 3,
    EVM_BAD_JUMP_DESTINATION = 4,
    EVM_STACK_OVERFLOW = 5,
    EVM_STACK_UNDERFLOW = 6,
};

struct evm_result;

/// Releases resources assigned to an execution result.
///
/// This function releases memory (and other resources, if any) assigned to the
/// specified execution result making the result object invalid.
///
/// @param result  The execution result which resource are to be released. The
///                result itself it not modified by this function, but becomes
///                invalid and user should discard it as well.
typedef void (*evm_release_result_fn)(struct evm_result const* result);

/// The EVM code execution result.
struct evm_result {
    /// The execution result code.
    enum evm_result_code code;

    /// The amount of gas left after the execution.
    ///
    /// The value is valid only if evm_result::code == ::EVM_SUCCESS.
    int64_t gas_left;

    union
    {
        struct
        {
            /// The reference to output data. The memory containing the output
            /// data is owned by EVM and is freed with evm_result::release().
            uint8_t const* output_data;

            /// The size of the output data.
            size_t output_size;
        };

        /// The address of the successfully created contract.
        ///
        /// This field has valid value only if the evm_result comes from a
        /// successful CREATE opcode execution
        /// (i.e. evm_call_fn(..., EVM_CREATE, ...)).
        struct evm_uint160be create_address;
    };

    /// The pointer to the result release implementation.
    ///
    /// This function pointer must be set by the VM implementation and works
    /// similary to C++ virtual destructor. Attaching the releaser to the result
    /// itself allows VM composition.
    evm_release_result_fn release;

    /// @name Optional
    /// The optional information that EVM is not required to provide.
    /// @{

    /// The error message explaining the result code.
    char const* error_message;

    /// The pointer to EVM-owned memory. For EVM internal use.
    /// @see output_data.
    void* internal_memory;

    /// @}
};

/// The query callback key.
enum evm_query_key {
    EVM_SLOAD = 0,            ///< Storage value of a given key for SLOAD.
    EVM_ADDRESS = 1,          ///< Address of the contract for ADDRESS.
    EVM_CALLER = 2,           ///< Message sender address for CALLER.
    EVM_ORIGIN = 3,           ///< Transaction origin address for ORIGIN.
    EVM_GAS_PRICE = 4,        ///< Transaction gas price for GASPRICE.
    EVM_COINBASE = 5,         ///< Current block miner address for COINBASE.
    EVM_DIFFICULTY = 6,       ///< Current block difficulty for DIFFICULTY.
    EVM_GAS_LIMIT = 7,        ///< Current block gas limit for GASLIMIT.
    EVM_NUMBER = 8,           ///< Current block number for NUMBER.
    EVM_TIMESTAMP = 9,        ///< Current block timestamp for TIMESTAMP.
    EVM_CODE_BY_ADDRESS = 10, ///< Code by an address for EXTCODECOPY.
    EVM_CODE_SIZE = 11,       ///< Code size by an address for EXTCODESIZE.
    EVM_BALANCE = 12,         ///< Balance of a given address for BALANCE.
    EVM_BLOCKHASH = 13,       ///< Block hash of by block number for BLOCKHASH.
    EVM_ACCOUNT_EXISTS = 14,  ///< Check if an account exists.
    EVM_CALL_DEPTH = 15,      ///< Current call depth.
};


/// Opaque struct representing execution enviroment managed by the host
/// application.
struct evm_env;

/// Variant type to represent possible types of values used in EVM.
///
/// Type-safety is lost around the code that uses this type. We should have
/// complete set of unit tests covering all possible cases.
/// The size of the type is 64 bytes and should fit in single cache line.
union evm_variant {
    /// A host-endian 64-bit integer.
    int64_t int64;

    /// A big-endian 256-bit integer or hash.
    struct evm_uint256be uint256be;

    struct {
        /// Additional padding to align the evm_variant::address with lower
        /// bytes of a full 256-bit hash.
        uint8_t address_padding[12];

        /// An Ethereum address.
        struct evm_uint160be address;
    };

    /// A memory reference.
    struct {
        /// Pointer to the data.
        uint8_t const* data;

        /// Size of the referenced memory/data.
        size_t data_size;
    };
};

/// Query callback function.
///
/// This callback function is used by the EVM to query the host application
/// about additional data required to execute EVM code.
/// @param env  Pointer to execution environment managed by the host
///             application.
/// @param key  The kind of the query. See evm_query_key and details below.
/// @param arg  Additional argument to the query. It has defined value only for
///             the subset of query keys.
///
/// ## Types of queries
///
/// - ::EVM_GAS_PRICE
///   @param arg n/a
///   @result evm_variant::uint256be  The transaction gas price.
///
/// - ::EVM_ADDRESS
///   @param arg n/a
///   @result evm_variant::address  The address of the current contract.
///
/// - ::EVM_CALLER
///   @param arg n/a
///   @result evm_variant::address  The address of the caller.
///
/// - ::EVM_ORIGIN
///   @param arg n/a
///   @result evm_variant::address  The address of the transaction initiator.
///
/// - ::EVM_COINBASE
///   @param arg n/a
///   @result evm_variant::address  The address of the beneficiary of the block fees.
///
/// - ::EVM_DIFFICULTY
///   @param arg n/a
///   @result evm_variant::uint256be  The block difficulty.
///
/// - ::EVM_GAS_LIMIT
///   @param arg n/a
///   @result evm_variant::uint256be  The block gas limit.
///
/// - ::EVM_NUMBER
///   @param arg n/a
///   @result evm_variant::int64  The block number.
///
/// - ::EVM_TIMESTAMP
///   @param arg n/a
///   @result evm_variant::int64  The block timestamp.
///
/// - ::EVM_CODE_BY_ADDRESS
///   @param arg evm_variant::address  The address to look up.
///   @result evm_variant::data  The appropriate code for the given address or NULL if not found.
///
/// - ::EVM_CODE_SIZE
///   @param arg evm_variant::address  The address to look up.
///   @result evm_variant::data  The appropriate code size for the given address or 0 if not found.
///
/// - ::EVM_BALANCE
///   @param arg evm_variant::address  The address to look up.
///   @result evm_variant::data  The appropriate balance for the given address or 0 if not found.
///
/// - ::EVM_BLOCKHASH
///   @param arg evm_variant::int64  The block number to look up.
///   @result evm_variant::uint256be  The hash of the requested block or 0 if not found.
///
/// - ::EVM_SLOAD
///   @param arg evm_variant::uint256be  The index of the storage entry.
///   @result evm_variant::uint256be  The current value of the storage entry.
///
typedef union evm_variant (*evm_query_fn)(struct evm_env* env,
                                          enum evm_query_key key,
                                          union evm_variant arg);

/// The update callback key.
enum evm_update_key {
    EVM_SSTORE = 0,        ///< Update storage entry
    EVM_LOG = 1,           ///< Log.
    EVM_SELFDESTRUCT = 2,  ///< Mark contract as selfdestructed and set
                           ///  beneficiary address.
};


/// Update callback function.
///
/// This callback function is used by the EVM to modify contract state in the
/// host application.
/// @param env  Pointer to execution environment managed by the host
///             application.
/// @param key  The kind of the update. See evm_update_key and details below.
///
/// ## Kinds of updates
///
/// - ::EVM_SSTORE
///   @param arg1 evm_variant::uint256be  The index of the storage entry.
///   @param arg2 evm_variant::uint256be  The value to be stored.
///
/// - ::EVM_LOG
///   @param arg1 evm_variant::data  The log unindexed data.
///   @param arg2 evm_variant::data  The log topics. The referenced data is an
///                                  array of evm_uint256be[] of possible length
///                                  from 0 to 4. So the valid
///                                  evm_variant::data_size values are 0, 32, 64
///                                  92 and 128.
///
/// - ::EVM_SELFDESTRUCT
///   @param arg1 evm_variant::address  The beneficiary address.
///   @param arg2 n/a
typedef void (*evm_update_fn)(struct evm_env* env,
                              enum evm_update_key key,
                              union evm_variant arg1,
                              union evm_variant arg2);

/// The kind of call-like instruction.
enum evm_call_kind {
    EVM_CALL = 0,         ///< Request CALL.
    EVM_DELEGATECALL = 1, ///< Request DELEGATECALL. The value param ignored.
    EVM_CALLCODE = 2,     ///< Request CALLCODE.
    EVM_CREATE = 3        ///< Request CREATE. Semantic of some params changes.
};

/// The flag indicating call failure in evm_call_fn().
static const int64_t EVM_CALL_FAILURE = INT64_MIN;

/// Pointer to the callback function supporting EVM calls.
///
/// @param env          Pointer to execution environment managed by the host
///                     application.
/// @param kind         The kind of call-like opcode requested.
/// @param gas          The amount of gas for the call.
/// @param address      The address of a contract to be called. Ignored in case
///                     of CREATE.
/// @param value        The value sent to the callee. The endowment in case of
///                     CREATE.
/// @param input        The call input data or the CREATE init code.
/// @param input_size   The size of the input data.
/// @param output       The reference to the memory where the call output is to
///                     be copied. In case of CREATE, the memory is guaranteed
///                     to be at least 20 bytes to hold the address of the
///                     created contract.
/// @param output_data  The size of the output data. In case of CREATE, expected
///                     value is 20.
/// @return      If non-negative - the amount of gas left,
///              If negative - an exception occurred during the call/create.
///              There is no need to set 0 address in the output in this case.
typedef int64_t (*evm_call_fn)(
    struct evm_env* env,
    enum evm_call_kind kind,
    int64_t gas,
    struct evm_uint160be address,
    struct evm_uint256be value,
    uint8_t const* input,
    size_t input_size,
    uint8_t* output,
    size_t output_size);


/// Opaque type representing a EVM instance.
struct evm_instance;

/// Creates new EVM instance.
///
/// Creates new EVM instance. The instance must be destroyed in evm_destroy().
/// Single instance is thread-safe and can be shared by many threads. Having
/// **multiple instances is safe but discouraged** as it has not benefits over
/// having the singleton.
///
/// @param query_fn   Pointer to query callback function. Nonnull.
/// @param update_fn  Pointer to update callback function. Nonnull.
/// @param call_fn    Pointer to call callback function. Nonnull.
/// @return           Pointer to the created EVM instance.
typedef struct evm_instance* (*evm_create_fn)(evm_query_fn query_fn,
                                              evm_update_fn update_fn,
                                              evm_call_fn call_fn);

/// Destroys the EVM instance.
///
/// @param evm  The EVM instance to be destroyed.
typedef void (*evm_destroy_fn)(struct evm_instance* evm);


/// Configures the EVM instance.
///
/// Allows modifying options of the EVM instance.
/// Options:
/// - code cache behavior: on, off, read-only, ...
/// - optimizations,
///
/// @param evm    The EVM instance to be configured.
/// @param name   The option name. NULL-terminated string. Cannot be NULL.
/// @param value  The new option value. NULL-terminated string. Cannot be NULL.
/// @return       1 if the option set successfully, 0 otherwise.
typedef int (*evm_set_option_fn)(struct evm_instance* evm,
                                 char const* name,
                                 char const* value);


/// EVM compatibility mode aka chain mode.
/// The names for the last two hard forks come from Python implementation.
enum evm_mode {
    EVM_FRONTIER = 0,
    EVM_HOMESTEAD = 1,
    EVM_ANTI_DOS = 2,
    EVM_CLEARING = 3
};


/// Generates and executes machine code for given EVM bytecode.
///
/// All the fun is here. This function actually does something useful.
///
/// @param instance    A EVM instance.
/// @param env         A pointer to the execution environment provided by the
///                    user and passed to callback functions.
/// @param mode        EVM compatibility mode.
/// @param code_hash   A hash of the bytecode, usually Keccak. The EVM uses it
///                    as the code identifier. A EVM implementation is able to
///                    hash the code itself if it requires it, but the host
///                    application usually has the hash already.
/// @param code        Reference to the bytecode to be executed.
/// @param code_size   The length of the bytecode.
/// @param gas         Gas for execution. Min 0, max 2^63-1.
/// @param input       Reference to the input data.
/// @param input_size  The size of the input data.
/// @param value       Call value.
/// @return            All execution results.
typedef struct evm_result (*evm_execute_fn)(struct evm_instance* instance,
                                            struct evm_env* env,
                                            enum evm_mode mode,
                                            struct evm_uint256be code_hash,
                                            uint8_t const* code,
                                            size_t code_size,
                                            int64_t gas,
                                            uint8_t const* input,
                                            size_t input_size,
                                            struct evm_uint256be value);


/// Status of a code in VM. Useful for JIT-like implementations.
enum evm_code_status {
    /// The code is uknown to the VM.
    EVM_UNKNOWN,

    /// The code has been compiled and is available in memory.
    EVM_READY,

    /// The compiled version of the code is available in on-disk cache.
    EVM_CACHED,
};


/// Get information the status of the code in the VM.
typedef enum evm_code_status
(*evm_get_code_status_fn)(struct evm_instance* instance,
                          enum evm_mode mode,
                          struct evm_uint256be code_hash);

/// Request preparation of the code for faster execution. It is not required
/// to execute the code but allows compilation of the code ahead of time in
/// JIT-like VMs.
typedef void (*evm_prepare_code_fn)(struct evm_instance* instance,
                                    enum evm_mode mode,
                                    struct evm_uint256be code_hash,
                                    uint8_t const* code,
                                    size_t code_size);

/// VM interface.
///
/// Defines the implementation of EVM-C interface for a VM.
struct evm_interface {
    /// EVM-C ABI version implemented by the VM.
    ///
    /// For future use to detect ABI incompatibilities. The EVM-C ABI version
    /// represented by this file is in ::EVM_ABI_VERSION.
    uint32_t abi_version;

    /// Pointer to function creating a VM's instance.
    evm_create_fn create;

    /// Pointer to function destroying a VM's instance.
    evm_destroy_fn destroy;

    /// Pointer to function execuing a code in a VM.
    evm_execute_fn execute;

    /// Optional pointer to function returning a status of a code.
    ///
    /// If the VM does not support this feature the pointer can be NULL.
    evm_get_code_status_fn get_code_status;

    /// Optional pointer to function compiling  a code.
    ///
    /// If the VM does not support this feature the pointer can be NULL.
    evm_prepare_code_fn prepare_code;

    /// Optional pointer to function modifying VM's options.
    ///
    /// If the VM does not support this feature the pointer can be NULL.
    evm_set_option_fn set_option;
};

// END Python CFFI declarations

/// Example of a function exporting an interface for an example VM.
///
/// Each VM implementation is obligates to provided a function returning
/// VM's interface.
/// The function has to be named as `<vm-name>_get_interface(void)`.
///
/// @return  VM interface
struct evm_interface examplevm_get_interface(void);


#if __cplusplus
}
#endif

#endif  // EVM_H
/// @}
