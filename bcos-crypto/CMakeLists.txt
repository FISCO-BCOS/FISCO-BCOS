project(bcos-crypto VERSION ${VERSION})

file(GLOB_RECURSE ALL_SRCS bcos-crypto/*.cpp)
set(LIB_LIST bcos-utilities TBB::tbb HSM-CRYPTO::sdf-crypto libsecp256k1::secp256k1 OpenSSL::SSL OpenSSL::Crypto)
if(ONLY_CPP_SDK)
    list(REMOVE_ITEM ALL_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/bcos-crypto/zkp/discretezkp/DiscreteLogarithmZkp.cpp)
    # the VRF and ZKP modules need libsodium, which is fullnode-only; nothing in the SDK
    # build consumes VRF or ZKP
    list(FILTER ALL_SRCS EXCLUDE REGEX ".*/bcos-crypto/vrf/.*\\.cpp$")
endif ()

find_package(fmt CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(TBB REQUIRED)
find_package(libsecp256k1 REQUIRED)
find_package(HSM-CRYPTO REQUIRED)
if(NOT ONLY_CPP_SDK)
    find_package(unofficial-sodium CONFIG REQUIRED)
    list(APPEND LIB_LIST unofficial-sodium::sodium)
endif()

add_library(bcos-crypto STATIC ${ALL_SRCS})
target_include_directories(bcos-crypto PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/bcos-crypto>)
target_link_libraries(bcos-crypto PUBLIC ${LIB_LIST} fmt::fmt)
target_link_libraries(bcos-crypto PUBLIC bcos-concepts)
# KZG (EIP-4844) is fullnode-only: the c-kzg-4844 port is a fullnode manifest feature, and
# its only consumers (mempool / engine / rpc) build in that configuration. PUBLIC so
# consumers of kzg/Kzg4844.h get ckzg's include dir and the archive on their link line.
if(FULLNODE)
    find_package(c-kzg-4844 CONFIG REQUIRED)
    target_link_libraries(bcos-crypto PUBLIC ckzg)
endif()

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
