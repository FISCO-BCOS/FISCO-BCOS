project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE ALL_SRCS bcos-crypto/*.cpp)
set(LIB_LIST wedprcrypto::crypto wedprcrypto::zkp bcos-utilities TBB::tbb HSM-CRYPTO::sdf-crypto libsecp256k1::secp256k1 OpenSSL::SSL OpenSSL::Crypto)
if(ONLY_CPP_SDK)
    list(REMOVE_ITEM ALL_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/bcos-crypto/zkp/discretezkp/DiscreteLogarithmZkp.cpp)
    list(REMOVE_ITEM LIB_LIST wedprcrypto::zkp)
endif ()

find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(TBB REQUIRED)
find_package(wedprcrypto REQUIRED)
find_package(libsecp256k1 REQUIRED)
find_package(HSM-CRYPTO REQUIRED)

add_library(bcos-crypto STATIC ${ALL_SRCS})
target_include_directories(bcos-crypto PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-crypto>)
target_link_libraries(bcos-crypto PUBLIC ${LIB_LIST} fmt::fmt)
target_link_libraries(bcos-crypto PUBLIC bcos-concepts)
set_target_properties(bcos-crypto PROPERTIES UNITY_BUILD "ON")

# Build-time regression guard against the bundled-secp256k1 link hijack: runs on every
# default build, including the -DTESTS=OFF release configuration that ships the affected
# binaries (the ctest of the same name only exists when TESTS is on). Registered here
# rather than at the top level because wedprcrypto's imported targets are
# directory-scoped and invisible from the root scope.
get_target_property(WEDPRCRYPTO_LIB_LOC wedprcrypto::crypto IMPORTED_LOCATION)
get_filename_component(WEDPRCRYPTO_LIB_DIR "${WEDPRCRYPTO_LIB_LOC}" DIRECTORY)
add_custom_target(check-wedpr-secp256k1 ALL
    COMMAND ${CMAKE_COMMAND} -DBINARY_DIR=${CMAKE_BINARY_DIR}
        "-DGENERATOR=${CMAKE_GENERATOR}" -DWEDPR_LIB_DIR=${WEDPRCRYPTO_LIB_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckWedprSecp256k1.cmake
    COMMENT "Checking the wedprcrypto link surface for bundled secp256k1")

if(NOT WIN32)
    target_compile_options(bcos-crypto PUBLIC "-Wno-ignored-attributes")
endif()

if(TESTS)
    enable_testing()
    add_subdirectory(test)
    add_subdirectory(demo)
endif()

include(GNUInstallDirs)
install(TARGETS bcos-crypto EXPORT fiscobcosTargets ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "bcos-crypto" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
