file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

hunter_add_package(evmc)
find_package(evmc CONFIG REQUIRED)
hunter_add_package(intx)
find_package(intx CONFIG REQUIRED)
hunter_add_package(ethash)
find_package(ethash CONFIG REQUIRED)
hunter_add_package(evmone)
find_package(evmone CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Boost CONFIG QUIET REQUIRED serialization program_options context)
find_package(bcos-crypto CONFIG QUIET REQUIRED)

add_library(executor ${SRC_LIST} ${HEADERS})

target_include_directories(executor PRIVATE ../include)
target_link_libraries(${EXECUTOR_TARGET} PUBLIC bcos-framework::codec bcos-framework::utilities bcos-crypto::bcos-crypto
    jsoncpp_lib_static Boost::serialization Boost::program_options TBB::tbb bcos-framework::storage Boost::context
    bcos-framework::protocol wedpr-crypto::crypto wedpr-crypto::extend-crypto evmone hera hera-buildinfo
    ${WASM_ENGINE_LIBRARY} evmc::loader evmc::instructions wabt)
target_compile_options(executor PRIVATE -Wextra -Wall -Wno-error -fno-var-tracking)

install_dependencies(executor)

