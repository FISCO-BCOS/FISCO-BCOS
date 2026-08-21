find_package(Boost REQUIRED unit_test_framework)
find_package(jsoncpp CONFIG REQUIRED)

# opstack-executor-tests was the repo's only GTest target; converted to Boost.Test to drop the
# gtest dependency (not in vcpkg.json, broke CI configure). BOOST_TEST_MODULE is defined in
# TestMain.cpp (the single main for the whole suite — the earlier three-target split left the
# add_test entries dangling with no add_executable; this restores the wiring with one target).
#
# No `ledger` link: header-only usage only (Classify.h), and the ledger→bcos-crypto→wedprcrypto
# chain breaks libc++ typed catch binary-wide (bcos-rpc/test/CMakeLists.txt:29-34), so
# exception-type assertions rely on exact-type BOOST_CHECK_THROW.
add_executable(opstack-executor-tests
    TestMain.cpp
    OpstackExecutorTest.cpp
    OpReceiptEncodeTest.cpp
    OpRlpDecodeTest.cpp
    OpDepositEnvelopeTest.cpp
    OpTxConvertTest.cpp
    SmokeCompileTest.cpp
    OpCommonTest.cpp
    OpCommitmentsTest.cpp
    RecentBlockHashesTest.cpp
    PreBlockOpStepsTest.cpp
    Storage2StateHelpersTest.cpp
    Storage2StatePoisonTest.cpp
    support/SeedPreStateTest.cpp
)
# OpReceiptEncodeTest diffs the FISCO receipt leaf encoding against evmone's independent
# evmone::state::rlp_encode (Log / TransactionReceipt overloads). bcos-evm deliberately keeps
# rlp_encode.cpp out of the production lib (zero production callers — see bcos-evm/CMakeLists.txt),
# so this test target compiles the package-shipped source itself; its <test/state/...> includes
# resolve through the pkg-include-shim dir that bcos-evm-eth exposes PUBLIC-ly (transitively on
# this target via opstack-executor → bcos-evm-opstack).
get_target_property(_evmone_include_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_include_dirs 0 _evmone_include_dir)
cmake_path(NORMAL_PATH _evmone_include_dir)
target_sources(opstack-executor-tests PRIVATE "${_evmone_include_dir}/test/utils/rlp_encode.cpp")
# Vendored upstream source: same warning exemption bcos-evm grants its ported files.
set_source_files_properties("${_evmone_include_dir}/test/utils/rlp_encode.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor
    rpc
    protocol-tars
    codec
    bcos-crypto
    bcos-task
    jsoncpp_static
    Boost::unit_test_framework
)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)
