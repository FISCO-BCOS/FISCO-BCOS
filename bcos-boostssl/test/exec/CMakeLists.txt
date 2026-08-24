# Sample / benchmark binaries under test/exec. They were previously dead code
# (nothing add_subdirectory'd this dir, and ${BOOSTSSL_TARGET} was undefined).
# They are now built whenever TESTS is on so the compile fixes do not rot again;
# they are not registered as ctest cases — run them manually for perf work.
# msg_codec_perf doubles as a golden smoke check (asserts byte-identical output).

add_executable(http-server-sample http_server_sample.cpp)
target_link_libraries(http-server-sample PUBLIC bcos-boostssl bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(echo-server-sample echo_server_sample.cpp)
target_link_libraries(echo-server-sample PUBLIC bcos-boostssl bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(echo-client-sample echo_client_sample.cpp)
target_link_libraries(echo-client-sample PUBLIC bcos-boostssl bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(msg-codec-perf msg_codec_perf.cpp)
target_link_libraries(msg-codec-perf PUBLIC bcos-boostssl bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(boostssl-delay-perf boostssl_delay_perf.cpp)
target_link_libraries(boostssl-delay-perf PUBLIC bcos-boostssl bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

add_executable(boostssl-throughput-perf boostssl_throughput_perf.cpp)
target_link_libraries(boostssl-throughput-perf PUBLIC bcos-boostssl bcos-utilities OpenSSL::SSL OpenSSL::Crypto)

# smoke check: run the golden byte-identical assertion of msg_codec_perf on every CI run
add_test(NAME msg-codec-perf-smoke COMMAND msg-codec-perf 1024 --fresh)
