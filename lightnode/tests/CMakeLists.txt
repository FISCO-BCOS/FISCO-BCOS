cmake_minimum_required(VERSION 3.17)

project(test-lightnode)

find_package(Boost REQUIRED unit_test_framework)

add_executable(test-lightnode LedgerServerImplTest.cpp StorageSyncWrapperTest.cpp main.cpp)
target_link_libraries(test-lightnode PUBLIC bcos-lightnode ${TABLE_TARGET} ${TARS_PROTOCOL_TARGET} Boost::unit_test_framework)

add_test(NAME test-lightnode COMMAND test-lightnode)