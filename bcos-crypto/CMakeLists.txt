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
# KZG (EIP-4844) is fullnode-only: the c-kzg-4844 port is a fullnode manifest feature, and
# its only consumers (mempool / engine / rpc) build in that configuration. PUBLIC so
# consumers of kzg/Kzg4844.h get ckzg's include dir and the archive on their link line.
if(FULLNODE)
    find_package(c-kzg-4844 CONFIG REQUIRED)
    target_link_libraries(bcos-crypto PUBLIC ckzg)
endif()
set_target_properties(bcos-crypto PROPERTIES UNITY_BUILD "ON")

# Build-time regression guard against the bundled-secp256k1 link hijack: runs on every
# default build, including the -DTESTS=OFF release configuration that ships the affected
# binaries (the ctest of the same name only exists when TESTS is on). Registered here
# rather than at the top level because wedprcrypto's imported targets are
# directory-scoped and invisible from the root scope. The inspection is Unix/GNU-only
# (libffi_c_*.a naming + nm): the windows-2025 CI job builds with -DTESTS=OFF on the
# x64-windows-static-release triplet, where the glob matches nothing and nm is absent,
# so an unconditional ALL target would abort that build — register only where the
# toolchain can actually be inspected.
find_program(NM_EXECUTABLE nm)
if(UNIX AND NM_EXECUTABLE)
    # The archive directory is resolved per configuration at build time through the
    # generator expression — a plain IMPORTED_LOCATION lookup at configure time misses
    # imported targets that only carry per-config IMPORTED_LOCATION_<CONFIG>.
    add_custom_target(check-wedpr-secp256k1 ALL
        COMMAND ${CMAKE_COMMAND} -DBINARY_DIR=${CMAKE_BINARY_DIR}
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DWEDPR_LIB_DIR=$<TARGET_FILE_DIR:wedprcrypto::crypto>"
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckWedprSecp256k1.cmake
        COMMENT "Checking the wedprcrypto link surface for bundled secp256k1")
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
