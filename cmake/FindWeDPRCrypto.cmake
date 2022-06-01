include(FindPackageHandleStandardArgs)
include(ExternalProject)
include(GNUInstallDirs)

set(WEDPR_CRYPTO_FOUND OFF)

add_library(wedpr::crypto MODULE IMPORTED)
add_library(wedpr::fisco-bcos MODULE IMPORTED)
if(NOT WEDPR_CRYPTO_ROOT_DIR)
  message(STATUS "Installing wedpr-crypto from github")
  set(WEDPR_CRYPTO_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-install")
  make_directory(${WEDPR_CRYPTO_INSTALL}/include)

  ExternalProject_Add(wedpr-crypto
    URL https://${URL_BASE}/WeBankBlockchain/WeDPR-Lab-Crypto/archive/0e3ca8614808825da4f91acc51e1031a5184119e.tar.gz
    URL_HASH SHA1=dcbb69c96085ada1d107380b3771fd8e177ad207
    CONFIGURE_COMMAND ""
    BUILD_COMMAND 
      cargo +nightly-2021-06-17 build --release
        --features "wedpr_f_hash_keccak256 wedpr_f_signature_secp256k1 wedpr_f_signature_sm2 wedpr_f_hash_sm3 wedpr_f_vrf_curve25519 wedpr_f_crypto_block_cipher_aes wedpr_f_crypto_block_cipher_sm4 wedpr_f_hash_ripemd160 wedpr_f_hash_sha2 wedpr_f_hash_sha3 wedpr_f_hash_blake2b wedpr_f_signature_ed25519"
        --no-default-features --manifest-path ${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-prefix/src/wedpr-crypto/ffi/ffi_c/ffi_c_crypto_binary/Cargo.toml;
      cargo +nightly-2021-06-17 build --release
        --manifest-path ${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-prefix/src/wedpr-crypto/ffi/ffi_c/ffi_c_fisco_bcos/Cargo.toml
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-prefix/src/wedpr-crypto/third_party/include" "${WEDPR_CRYPTO_INSTALL}/include/wedpr-crypto"
  )

  set(WEDPR_CRYPTO_INCLUDE_DIRS "${WEDPR_CRYPTO_INSTALL}/include/")
  set(WEDPR_CRYPTO_crypto_LIBRARIES "${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-prefix/src/wedpr-crypto/target/release/${CMAKE_STATIC_LIBRARY_PREFIX}ffi_c_crypto_binary${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(WEDPR_CRYPTO_fisco-bcos_LIBRARIES "${CMAKE_CURRENT_BINARY_DIR}/wedpr-crypto-prefix/src/wedpr-crypto/target/release/${CMAKE_STATIC_LIBRARY_PREFIX}ffi_c_fisco-bcos_binary${CMAKE_STATIC_LIBRARY_SUFFIX}")
  
  add_dependencies(wedpr::crypto wedpr-crypto)
  add_dependencies(wedpr::fisco-bcos wedpr-crypto)
else()
  message(STATUS "Find wedpr-crypto in ${WEDPR_CRYPTO_ROOT_DIR}")
  find_path(WEDPR_CRYPTO_INCLUDE_DIRS NAMES wedpr-crypto PATHS ${WEDPR_CRYPTO_ROOT_DIR}/include REQUIRED)
  find_library(WEDPR_CRYPTO_crypto_LIBRARIES NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}ffi_c_crypto_binary${CMAKE_STATIC_LIBRARY_SUFFIX}}
    PATHS ${WEDPR_CRYPTO_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  find_library(WEDPR_CRYPTO_fisco-bcos_LIBRARIES NAMES ${CMAKE_STATIC_LIBRARY_PREFIX}ffi_c_fisco_bcos${CMAKE_STATIC_LIBRARY_SUFFIX}}
    PATHS ${WEDPR_CRYPTO_ROOT_DIR}/${CMAKE_INSTALL_LIBDIR} REQUIRED)
  message(STATUS "Found wedpr-crypto include dir: ${WEDPR_CRYPTO_INCLUDE_DIRS} lib dir: ${WEDPR_CRYPTO_LIBRARIES}")
endif()

target_include_directories(wedpr::crypto INTERFACE ${WEDPR_CRYPTO_INCLUDE_DIRS})
set_property(TARGET wedpr::crypto PROPERTY IMPORTED_LOCATION ${WEDPR_CRYPTO_crypto_LIBRARIES})
target_include_directories(wedpr::fisco-bcos INTERFACE ${WEDPR_CRYPTO_INCLUDE_DIRS})
set_property(TARGET wedpr::fisco-bcos PROPERTY IMPORTED_LOCATION ${WEDPR_CRYPTO_fisco-bcos_LIBRARIES})

set(WEDPR_CRYPTO_FOUND ON)