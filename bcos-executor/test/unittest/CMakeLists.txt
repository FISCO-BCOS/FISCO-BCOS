if(WITH_WASM)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libvm/WasmPath.h.in ${CMAKE_CURRENT_SOURCE_DIR}/libvm/WasmPath.h @ONLY)
    set(EXCLUDE_SUITES "")
else()
    set(EXCLUDE_SUITES "TestWasmExecutor")
endif()
file(GLOB_RECURSE SOURCES "*.cpp" "*.h")

# cmake settings
set(TEST_BINARY_NAME test-bcos-executor)

add_executable(${TEST_BINARY_NAME} ${SOURCES} libprecompiled/PrecompiledCallTest.cpp)
target_include_directories(${TEST_BINARY_NAME} PRIVATE ./ ../../src ../../include)

find_package(Boost REQUIRED unit_test_framework)
find_package(TBB CONFIG REQUIRED)
include(SearchTestCases)

target_link_libraries(${TEST_BINARY_NAME} ${EXECUTOR_TARGET} ${LEDGER_TARGET} ${CRYPTO_TARGET} ${TARS_PROTOCOL_TARGET} bcos-framework Boost::serialization Boost::unit_test_framework)
target_compile_options(${TEST_BINARY_NAME} PRIVATE -fno-var-tracking -Wno-unused-parameter)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(${TEST_BINARY_NAME} PROPERTIES UNITY_BUILD "ON")
config_test_cases("" "${SOURCES}" ${TEST_BINARY_NAME} "${EXCLUDE_SUITES}")