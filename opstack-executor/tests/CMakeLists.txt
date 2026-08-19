find_package(Boost REQUIRED unit_test_framework)
find_package(evmone REQUIRED)
# rlp_encode.cpp left the production lib (zero callers); its only consumer, OpReceiptEncodeTest,
# ADL-calls evmone::state::rlp_encode. Re-derive EVMONE_TEST_UTILS_DIR here — set only in sibling bcos-evm scope.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_inc_dirs 0 _evmone_inc_dir)
cmake_path(NORMAL_PATH _evmone_inc_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_inc_dir}/test/utils")

# opstack-executor-tests was the repo's only GTest target; converted to Boost.Test to drop the
# gtest dependency (not in vcpkg.json, broke CI configure). BOOST_TEST_MODULE is defined in
# TestMain.cpp (the single main for the whole suite — the earlier three-target split left the
# add_test entries dangling with no add_executable; this restores the wiring with one target).
find_package(jsoncpp CONFIG REQUIRED)
add_executable(opstack-executor-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    Storage2StateHelpersTest.cpp
    Storage2StatePoisonTest.cpp
    support/SeedPreStateTest.cpp
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor
    ledger
    jsoncpp_static
    Boost::unit_test_framework
)
set_source_files_properties(${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)
